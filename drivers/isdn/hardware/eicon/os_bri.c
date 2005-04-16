/* $Id: os_bri.c,v 1.21 2004/03/21 17:26:01 armin Exp $ */

#include "platform.h"
#include "debuglib.h"
#include "cardtype.h"
#include "pc.h"
#include "pr_pc.h"
#include "di_defs.h"
#include "dsp_defs.h"
#include "di.h"
#include "io.h"

#include "xdi_msg.h"
#include "xdi_adapter.h"
#include "os_bri.h"
#include "diva_pci.h"
#include "mi_pc.h"
#include "pc_maint.h"

/*
**  IMPORTS
*/
extern void prepare_maestra_functions(PISDN_ADAPTER IoAdapter);
extern void diva_xdi_display_adapter_features(int card);
extern int diva_card_read_xlog(diva_os_xdi_adapter_t * a);

/*
**  LOCALS
*/
static int bri_bar_length[3] = {
	0x80,
	0x80,
	0x20
};
static int diva_bri_cleanup_adapter(diva_os_xdi_adapter_t * a);
static dword diva_bri_get_serial_number(diva_os_xdi_adapter_t * a);
static int diva_bri_cmd_card_proc(struct _diva_os_xdi_adapter *a,
				  diva_xdi_um_cfg_cmd_t * cmd, int length);
static int diva_bri_reregister_io(diva_os_xdi_adapter_t * a);
static int diva_bri_reset_adapter(PISDN_ADAPTER IoAdapter);
static int diva_bri_write_sdram_block(PISDN_ADAPTER IoAdapter,
				      dword address,
				      const byte * data, dword length);
static int diva_bri_start_adapter(PISDN_ADAPTER IoAdapter,
				  dword start_address, dword features);
static int diva_bri_stop_adapter(diva_os_xdi_adapter_t * a);

static void diva_bri_set_addresses(diva_os_xdi_adapter_t * a)
{
	a->resources.pci.mem_type_id[MEM_TYPE_RAM] = 0;
	a->resources.pci.mem_type_id[MEM_TYPE_CFG] = 1;
	a->resources.pci.mem_type_id[MEM_TYPE_ADDRESS] = 2;
	a->resources.pci.mem_type_id[MEM_TYPE_RESET] = 1;
	a->resources.pci.mem_type_id[MEM_TYPE_PORT] = 2;
	a->resources.pci.mem_type_id[MEM_TYPE_CTLREG] = 2;
	
	a->xdi_adapter.ram = a->resources.pci.addr[0];
	a->xdi_adapter.cfg = a->resources.pci.addr[1];
	a->xdi_adapter.Address = a->resources.pci.addr[2];

	a->xdi_adapter.reset = a->xdi_adapter.cfg;
	a->xdi_adapter.port = a->xdi_adapter.Address;

	a->xdi_adapter.ctlReg = a->xdi_adapter.port + M_PCI_RESET;

	a->xdi_adapter.reset += 0x4C;	/* PLX 9050 !! */
}

/*
**  BAR0 - MEM Addr  - 0x80  - NOT USED
**  BAR1 - I/O Addr  - 0x80
**  BAR2 - I/O Addr  - 0x20
*/
int diva_bri_init_card(diva_os_xdi_adapter_t * a)
{
	int bar;
	dword bar2 = 0, bar2_length = 0xffffffff;
	word cmd = 0, cmd_org;
	byte Bus, Slot;
	void *hdev;
	byte __iomem *p;

	/*
	   Set properties
	 */
	a->xdi_adapter.Properties = CardProperties[a->CardOrdinal];
	DBG_LOG(("Load %s", a->xdi_adapter.Properties.Name))

	    /*
	       Get resources
	     */
	    for (bar = 0; bar < 3; bar++) {
		a->resources.pci.bar[bar] =
		    divasa_get_pci_bar(a->resources.pci.bus,
				       a->resources.pci.func, bar,
				       a->resources.pci.hdev);
		if (!a->resources.pci.bar[bar]) {
			DBG_ERR(("A: can't get BAR[%d]", bar))
			return (-1);
		}
	}

	a->resources.pci.irq =
	    (byte) divasa_get_pci_irq(a->resources.pci.bus,
				      a->resources.pci.func,
				      a->resources.pci.hdev);
	if (!a->resources.pci.irq) {
		DBG_ERR(("A: invalid irq"));
		return (-1);
	}

	/*
	   Get length of I/O bar 2 - it is different by older
	   EEPROM version
	 */
	Bus = a->resources.pci.bus;
	Slot = a->resources.pci.func;
	hdev = a->resources.pci.hdev;

	/*
	   Get plain original values of the BAR2 CDM registers
	 */
	PCIread(Bus, Slot, 0x18, &bar2, sizeof(bar2), hdev);
	PCIread(Bus, Slot, 0x04, &cmd_org, sizeof(cmd_org), hdev);
	/*
	   Disable device and get BAR2 length
	 */
	PCIwrite(Bus, Slot, 0x04, &cmd, sizeof(cmd), hdev);
	PCIwrite(Bus, Slot, 0x18, &bar2_length, sizeof(bar2_length), hdev);
	PCIread(Bus, Slot, 0x18, &bar2_length, sizeof(bar2_length), hdev);
	/*
	   Restore BAR2 and CMD registers
	 */
	PCIwrite(Bus, Slot, 0x18, &bar2, sizeof(bar2), hdev);
	PCIwrite(Bus, Slot, 0x04, &cmd_org, sizeof(cmd_org), hdev);

	/*
	   Calculate BAR2 length
	 */
	bar2_length = (~(bar2_length & ~7)) + 1;
	DBG_LOG(("BAR[2] length=%lx", bar2_length))

	    /*
	       Map and register resources
	     */
	    if (!(a->resources.pci.addr[0] =
		 divasa_remap_pci_bar(a, 0, a->resources.pci.bar[0],
				      bri_bar_length[0]))) {
		DBG_ERR(("A: BRI, can't map BAR[0]"))
		diva_bri_cleanup_adapter(a);
		return (-1);
	}

	sprintf(&a->port_name[0], "BRI %02x:%02x",
		a->resources.pci.bus, a->resources.pci.func);

	if (diva_os_register_io_port(a, 1, a->resources.pci.bar[1],
				     bri_bar_length[1], &a->port_name[0], 1)) {
		DBG_ERR(("A: BRI, can't register BAR[1]"))
		diva_bri_cleanup_adapter(a);
		return (-1);
	}
	a->resources.pci.addr[1] = (void *) (unsigned long) a->resources.pci.bar[1];
	a->resources.pci.length[1] = bri_bar_length[1];

	if (diva_os_register_io_port(a, 1, a->resources.pci.bar[2],
				     bar2_length, &a->port_name[0], 2)) {
		DBG_ERR(("A: BRI, can't register BAR[2]"))
		diva_bri_cleanup_adapter(a);
		return (-1);
	}
	a->resources.pci.addr[2] = (void *) (unsigned long) a->resources.pci.bar[2];
	a->resources.pci.length[2] = bar2_length;

	/*
	   Set all memory areas
	 */
	diva_bri_set_addresses(a);

	/*
	   Get Serial Number
	 */
	a->xdi_adapter.serialNo = diva_bri_get_serial_number(a);

	/*
	   Register I/O ports with correct name now
	 */
	if (diva_bri_reregister_io(a)) {
		diva_bri_cleanup_adapter(a);
		return (-1);
	}

	/*
	   Initialize OS dependent objects
	 */
	if (diva_os_initialize_spin_lock
	    (&a->xdi_adapter.isr_spin_lock, "isr")) {
		diva_bri_cleanup_adapter(a);
		return (-1);
	}
	if (diva_os_initialize_spin_lock
	    (&a->xdi_adapter.data_spin_lock, "data")) {
		diva_bri_cleanup_adapter(a);
		return (-1);
	}

	strcpy(a->xdi_adapter.req_soft_isr.dpc_thread_name, "kdivasbrid");

	if (diva_os_initialize_soft_isr(&a->xdi_adapter.req_soft_isr,
					DIDpcRoutine, &a->xdi_adapter)) {
		diva_bri_cleanup_adapter(a);
		return (-1);
	}
	/*
	   Do not initialize second DPC - only one thread will be created
	 */
	a->xdi_adapter.isr_soft_isr.object = a->xdi_adapter.req_soft_isr.object;

	/*
	   Create entity table
	 */
	a->xdi_adapter.Channels = CardProperties[a->CardOrdinal].Channels;
	a->xdi_adapter.e_max = CardProperties[a->CardOrdinal].E_info;
	a->xdi_adapter.e_tbl = diva_os_malloc(0, a->xdi_adapter.e_max * sizeof(E_INFO));
	if (!a->xdi_adapter.e_tbl) {
		diva_bri_cleanup_adapter(a);
		return (-1);
	}
	memset(a->xdi_adapter.e_tbl, 0x00, a->xdi_adapter.e_max * sizeof(E_INFO));

	/*
	   Set up interface
	 */
	a->xdi_adapter.a.io = &a->xdi_adapter;
	a->xdi_adapter.DIRequest = request;
	a->interface.cleanup_adapter_proc = diva_bri_cleanup_adapter;
	a->interface.cmd_proc = diva_bri_cmd_card_proc;

	p = DIVA_OS_MEM_ATTACH_RESET(&a->xdi_adapter);
	outpp(p, 0x41);
	DIVA_OS_MEM_DETACH_RESET(&a->xdi_adapter, p);

	prepare_maestra_functions(&a->xdi_adapter);

	a->dsp_mask = 0x00000003;

	/*
	   Set IRQ handler
	 */
	a->xdi_adapter.irq_info.irq_nr = a->resources.pci.irq;
	sprintf(a->xdi_adapter.irq_info.irq_name, "DIVA BRI %ld",
		(long) a->xdi_adapter.serialNo);
	if (diva_os_register_irq(a, a->xdi_adapter.irq_info.irq_nr,
				 a->xdi_adapter.irq_info.irq_name)) {
		diva_bri_cleanup_adapter(a);
		return (-1);
	}
	a->xdi_adapter.irq_info.registered = 1;

	diva_log_info("%s IRQ:%d SerNo:%d", a->xdi_adapter.Properties.Name,
		      a->resources.pci.irq, a->xdi_adapter.serialNo);

	return (0);
}


static int diva_bri_cleanup_adapter(diva_os_xdi_adapter_t * a)
{
	int i;

	if (a->xdi_adapter.Initialized) {
		diva_bri_stop_adapter(a);
	}

	/*
	   Remove ISR Handler
	 */
	if (a->xdi_adapter.irq_info.registered) {
		diva_os_remove_irq(a, a->xdi_adapter.irq_info.irq_nr);
	}
	a->xdi_adapter.irq_info.registered = 0;

	if (a->resources.pci.addr[0] && a->resources.pci.bar[0]) {
		divasa_unmap_pci_bar(a->resources.pci.addr[0]);
		a->resources.pci.addr[0] = NULL;
		a->resources.pci.bar[0] = 0;
	}

	for (i = 1; i < 3; i++) {
		if (a->resources.pci.addr[i] && a->resources.pci.bar[i]) {
			diva_os_register_io_port(a, 0,
						 a->resources.pci.bar[i],
						 a->resources.pci.
						 length[i],
						 &a->port_name[0], i);
			a->resources.pci.addr[i] = NULL;
			a->resources.pci.bar[i] = 0;
		}
	}

	/*
	   Free OS objects
	 */
	diva_os_cancel_soft_isr(&a->xdi_adapter.req_soft_isr);
	diva_os_cancel_soft_isr(&a->xdi_adapter.isr_soft_isr);

	diva_os_remove_soft_isr(&a->xdi_adapter.req_soft_isr);
	a->xdi_adapter.isr_soft_isr.object = NULL;

	diva_os_destroy_spin_lock(&a->xdi_adapter.isr_spin_lock, "rm");
	diva_os_destroy_spin_lock(&a->xdi_adapter.data_spin_lock, "rm");

	/*
	   Free memory
	 */
	if (a->xdi_adapter.e_tbl) {
		diva_os_free(0, a->xdi_adapter.e_tbl);
		a->xdi_adapter.e_tbl = NULL;
	}

	return (0);
}

void diva_os_prepare_maestra_functions(PISDN_ADAPTER IoAdapter)
{
}

/*
**  Get serial number
*/
static dword diva_bri_get_serial_number(diva_os_xdi_adapter_t * a)
{
	dword serNo = 0;
	byte __iomem *confIO;
	word serHi, serLo;
	word __iomem *confMem;

	confIO = DIVA_OS_MEM_ATTACH_CFG(&a->xdi_adapter);
	serHi = (word) (inppw(&confIO[0x22]) & 0x0FFF);
	serLo = (word) (inppw(&confIO[0x26]) & 0x0FFF);
	serNo = ((dword) serHi << 16) | (dword) serLo;
	DIVA_OS_MEM_DETACH_CFG(&a->xdi_adapter, confIO);

	if ((serNo == 0) || (serNo == 0xFFFFFFFF)) {
		DBG_FTL(("W: BRI use BAR[0] to get card serial number"))

		confMem = (word __iomem *)DIVA_OS_MEM_ATTACH_RAM(&a->xdi_adapter);
		serHi = (word) (READ_WORD(&confMem[0x11]) & 0x0FFF);
		serLo = (word) (READ_WORD(&confMem[0x13]) & 0x0FFF);
		serNo = (((dword) serHi) << 16) | ((dword) serLo);
		DIVA_OS_MEM_DETACH_RAM(&a->xdi_adapter, confMem);
	}

	DBG_LOG(("Serial Number=%ld", serNo))

	return (serNo);
}

/*
**  Unregister I/O and register it with new name,
**  based on Serial Number
*/
static int diva_bri_reregister_io(diva_os_xdi_adapter_t * a)
{
	int i;

	for (i = 1; i < 3; i++) {
		diva_os_register_io_port(a, 0, a->resources.pci.bar[i],
					 a->resources.pci.length[i],
					 &a->port_name[0], i);
		a->resources.pci.addr[i] = NULL;
	}

	sprintf(a->port_name, "DIVA BRI %ld",
		(long) a->xdi_adapter.serialNo);

	for (i = 1; i < 3; i++) {
		if (diva_os_register_io_port(a, 1, a->resources.pci.bar[i],
					     a->resources.pci.length[i],
					     &a->port_name[0], i)) {
			DBG_ERR(("A: failed to reregister BAR[%d]", i))
			return (-1);
		}
		a->resources.pci.addr[i] =
		    (void *) (unsigned long) a->resources.pci.bar[i];
	}

	return (0);
}

/*
**  Process command from user mode
*/
static int
diva_bri_cmd_card_proc(struct _diva_os_xdi_adapter *a,
		       diva_xdi_um_cfg_cmd_t * cmd, int length)
{
	int ret = -1;

	if (cmd->adapter != a->controller) {
		DBG_ERR(("A: pri_cmd, invalid controller=%d != %d",
			 cmd->adapter, a->controller))
		return (-1);
	}

	switch (cmd->command) {
	case DIVA_XDI_UM_CMD_GET_CARD_ORDINAL:
		a->xdi_mbox.data_length = sizeof(dword);
		a->xdi_mbox.data =
		    diva_os_malloc(0, a->xdi_mbox.data_length);
		if (a->xdi_mbox.data) {
			*(dword *) a->xdi_mbox.data =
			    (dword) a->CardOrdinal;
			a->xdi_mbox.status = DIVA_XDI_MBOX_BUSY;
			ret = 0;
		}
		break;

	case DIVA_XDI_UM_CMD_GET_SERIAL_NR:
		a->xdi_mbox.data_length = sizeof(dword);
		a->xdi_mbox.data =
		    diva_os_malloc(0, a->xdi_mbox.data_length);
		if (a->xdi_mbox.data) {
			*(dword *) a->xdi_mbox.data =
			    (dword) a->xdi_adapter.serialNo;
			a->xdi_mbox.status = DIVA_XDI_MBOX_BUSY;
			ret = 0;
		}
		break;

	case DIVA_XDI_UM_CMD_GET_PCI_HW_CONFIG:
		a->xdi_mbox.data_length = sizeof(dword) * 9;
		a->xdi_mbox.data =
		    diva_os_malloc(0, a->xdi_mbox.data_length);
		if (a->xdi_mbox.data) {
			int i;
			dword *data = (dword *) a->xdi_mbox.data;

			for (i = 0; i < 8; i++) {
				*data++ = a->resources.pci.bar[i];
			}
			*data++ = (dword) a->resources.pci.irq;
			a->xdi_mbox.status = DIVA_XDI_MBOX_BUSY;
			ret = 0;
		}
		break;

	case DIVA_XDI_UM_CMD_GET_CARD_STATE:
		a->xdi_mbox.data_length = sizeof(dword);
		a->xdi_mbox.data =
		    diva_os_malloc(0, a->xdi_mbox.data_length);
		if (a->xdi_mbox.data) {
			dword *data = (dword *) a->xdi_mbox.data;
			if (!a->xdi_adapter.port) {
				*data = 3;
			} else if (a->xdi_adapter.trapped) {
				*data = 2;
			} else if (a->xdi_adapter.Initialized) {
				*data = 1;
			} else {
				*data = 0;
			}
			a->xdi_mbox.status = DIVA_XDI_MBOX_BUSY;
			ret = 0;
		}
		break;

	case DIVA_XDI_UM_CMD_RESET_ADAPTER:
		ret = diva_bri_reset_adapter(&a->xdi_adapter);
		break;

	case DIVA_XDI_UM_CMD_WRITE_SDRAM_BLOCK:
		ret = diva_bri_write_sdram_block(&a->xdi_adapter,
						 cmd->command_data.
						 write_sdram.offset,
						 (byte *) & cmd[1],
						 cmd->command_data.
						 write_sdram.length);
		break;

	case DIVA_XDI_UM_CMD_START_ADAPTER:
		ret = diva_bri_start_adapter(&a->xdi_adapter,
					     cmd->command_data.start.
					     offset,
					     cmd->command_data.start.
					     features);
		break;

	case DIVA_XDI_UM_CMD_SET_PROTOCOL_FEATURES:
		a->xdi_adapter.features =
		    cmd->command_data.features.features;
		a->xdi_adapter.a.protocol_capabilities =
		    a->xdi_adapter.features;
		DBG_TRC(
			("Set raw protocol features (%08x)",
			 a->xdi_adapter.features)) ret = 0;
		break;

	case DIVA_XDI_UM_CMD_STOP_ADAPTER:
		ret = diva_bri_stop_adapter(a);
		break;

	case DIVA_XDI_UM_CMD_READ_XLOG_ENTRY:
		ret = diva_card_read_xlog(a);
		break;

	default:
		DBG_ERR(
			("A: A(%d) invalid cmd=%d", a->controller,
			 cmd->command))}

	return (ret);
}

static int diva_bri_reset_adapter(PISDN_ADAPTER IoAdapter)
{
	byte __iomem *addrHi, *addrLo, *ioaddr;
	dword i;
	byte __iomem *Port;

	if (!IoAdapter->port) {
		return (-1);
	}
	if (IoAdapter->Initialized) {
		DBG_ERR(("A: A(%d) can't reset BRI adapter - please stop first",
			 IoAdapter->ANum)) return (-1);
	}
	(*(IoAdapter->rstFnc)) (IoAdapter);
	diva_os_wait(100);
	Port = DIVA_OS_MEM_ATTACH_PORT(IoAdapter);
	addrHi = Port +
	    ((IoAdapter->Properties.Bus == BUS_PCI) ? M_PCI_ADDRH : ADDRH);
	addrLo = Port + ADDR;
	ioaddr = Port + DATA;
	/*
	   recover
	 */
	outpp(addrHi, (byte) 0);
	outppw(addrLo, (word) 0);
	outppw(ioaddr, (word) 0);
	/*
	   clear shared memory
	 */
	outpp(addrHi,
	      (byte) (
		      (IoAdapter->MemoryBase + IoAdapter->MemorySize -
		       BRI_SHARED_RAM_SIZE) >> 16));
	outppw(addrLo, 0);
	for (i = 0; i < 0x8000; outppw(ioaddr, 0), ++i);
	diva_os_wait(100);

	/*
	   clear signature
	 */
	outpp(addrHi,
	      (byte) (
		      (IoAdapter->MemoryBase + IoAdapter->MemorySize -
		       BRI_SHARED_RAM_SIZE) >> 16));
	outppw(addrLo, 0x1e);
	outpp(ioaddr, 0);
	outpp(ioaddr, 0);

	outpp(addrHi, (byte) 0);
	outppw(addrLo, (word) 0);
	outppw(ioaddr, (word) 0);

	DIVA_OS_MEM_DETACH_PORT(IoAdapter, Port);

	/*
	   Forget all outstanding entities
	 */
	IoAdapter->e_count = 0;
	if (IoAdapter->e_tbl) {
		memset(IoAdapter->e_tbl, 0x00,
		       IoAdapter->e_max * sizeof(E_INFO));
	}
	IoAdapter->head = 0;
	IoAdapter->tail = 0;
	IoAdapter->assign = 0;
	IoAdapter->trapped = 0;

	memset(&IoAdapter->a.IdTable[0], 0x00,
	       sizeof(IoAdapter->a.IdTable));
	memset(&IoAdapter->a.IdTypeTable[0], 0x00,
	       sizeof(IoAdapter->a.IdTypeTable));
	memset(&IoAdapter->a.FlowControlIdTable[0], 0x00,
	       sizeof(IoAdapter->a.FlowControlIdTable));
	memset(&IoAdapter->a.FlowControlSkipTable[0], 0x00,
	       sizeof(IoAdapter->a.FlowControlSkipTable));
	memset(&IoAdapter->a.misc_flags_table[0], 0x00,
	       sizeof(IoAdapter->a.misc_flags_table));
	memset(&IoAdapter->a.rx_stream[0], 0x00,
	       sizeof(IoAdapter->a.rx_stream));
	memset(&IoAdapter->a.tx_stream[0], 0x00,
	       sizeof(IoAdapter->a.tx_stream));
	memset(&IoAdapter->a.tx_pos[0], 0x00, sizeof(IoAdapter->a.tx_pos));
	memset(&IoAdapter->a.rx_pos[0], 0x00, sizeof(IoAdapter->a.rx_pos));

	return (0);
}

static int
diva_bri_write_sdram_block(PISDN_ADAPTER IoAdapter,
			   dword address, const byte * data, dword length)
{
	byte __iomem *addrHi, *addrLo, *ioaddr;
	byte __iomem *Port;

	if (!IoAdapter->port) {
		return (-1);
	}

	Port = DIVA_OS_MEM_ATTACH_PORT(IoAdapter);
	addrHi = Port +
	    ((IoAdapter->Properties.Bus == BUS_PCI) ? M_PCI_ADDRH : ADDRH);
	addrLo = Port + ADDR;
	ioaddr = Port + DATA;

	while (length--) {
		outpp(addrHi, (word) (address >> 16));
		outppw(addrLo, (word) (address & 0x0000ffff));
		outpp(ioaddr, *data++);
		address++;
	}

	DIVA_OS_MEM_DETACH_PORT(IoAdapter, Port);
	return (0);
}

static int
diva_bri_start_adapter(PISDN_ADAPTER IoAdapter,
		       dword start_address, dword features)
{
	byte __iomem *Port;
	dword i, test;
	byte __iomem *addrHi, *addrLo, *ioaddr;
	int started = 0;
	ADAPTER *a = &IoAdapter->a;

	if (IoAdapter->Initialized) {
		DBG_ERR(
			("A: A(%d) bri_start_adapter, adapter already running",
			 IoAdapter->ANum)) return (-1);
	}
	if (!IoAdapter->port) {
		DBG_ERR(("A: A(%d) bri_start_adapter, adapter not mapped",
			 IoAdapter->ANum)) return (-1);
	}

	sprintf(IoAdapter->Name, "A(%d)", (int) IoAdapter->ANum);
	DBG_LOG(("A(%d) start BRI", IoAdapter->ANum))

	Port = DIVA_OS_MEM_ATTACH_PORT(IoAdapter);
	addrHi = Port +
	    ((IoAdapter->Properties.Bus == BUS_PCI) ? M_PCI_ADDRH : ADDRH);
	addrLo = Port + ADDR;
	ioaddr = Port + DATA;

	outpp(addrHi,
	      (byte) (
		      (IoAdapter->MemoryBase + IoAdapter->MemorySize -
		       BRI_SHARED_RAM_SIZE) >> 16));
	outppw(addrLo, 0x1e);
	outppw(ioaddr, 0x00);
	DIVA_OS_MEM_DETACH_PORT(IoAdapter, Port);

	/*
	   start the protocol code
	 */
	Port = DIVA_OS_MEM_ATTACH_CTLREG(IoAdapter);
	outpp(Port, 0x08);
	DIVA_OS_MEM_DETACH_CTLREG(IoAdapter, Port);

	Port = DIVA_OS_MEM_ATTACH_PORT(IoAdapter);
	addrHi = Port +
	    ((IoAdapter->Properties.Bus == BUS_PCI) ? M_PCI_ADDRH : ADDRH);
	addrLo = Port + ADDR;
	ioaddr = Port + DATA;
	/*
	   wait for signature (max. 3 seconds)
	 */
	for (i = 0; i < 300; ++i) {
		diva_os_wait(10);
		outpp(addrHi,
		      (byte) (
			      (IoAdapter->MemoryBase +
			       IoAdapter->MemorySize -
			       BRI_SHARED_RAM_SIZE) >> 16));
		outppw(addrLo, 0x1e);
		test = (dword) inppw(ioaddr);
		if (test == 0x4447) {
			DBG_LOG(
				("Protocol startup time %d.%02d seconds",
				 (i / 100), (i % 100)))
			started = 1;
			break;
		}
	}
	DIVA_OS_MEM_DETACH_PORT(IoAdapter, Port);

	if (!started) {
		DBG_FTL(("A: A(%d) %s: Adapter selftest failed 0x%04X",
			 IoAdapter->ANum, IoAdapter->Properties.Name,
			 test))
		(*(IoAdapter->trapFnc)) (IoAdapter);
		return (-1);
	}

	IoAdapter->Initialized = 1;

	/*
	   Check Interrupt
	 */
	IoAdapter->IrqCount = 0;
	a->ReadyInt = 1;

	if (IoAdapter->reset) {
		Port = DIVA_OS_MEM_ATTACH_RESET(IoAdapter);
		outpp(Port, 0x41);
		DIVA_OS_MEM_DETACH_RESET(IoAdapter, Port);
	}

	a->ram_out(a, &PR_RAM->ReadyInt, 1);
	for (i = 0; ((!IoAdapter->IrqCount) && (i < 100)); i++) {
		diva_os_wait(10);
	}
	if (!IoAdapter->IrqCount) {
		DBG_ERR(
			("A: A(%d) interrupt test failed",
			 IoAdapter->ANum))
		IoAdapter->Initialized = 0;
		IoAdapter->stop(IoAdapter);
		return (-1);
	}

	IoAdapter->Properties.Features = (word) features;
	diva_xdi_display_adapter_features(IoAdapter->ANum);
	DBG_LOG(("A(%d) BRI adapter successfull started", IoAdapter->ANum))
	    /*
	       Register with DIDD
	     */
	diva_xdi_didd_register_adapter(IoAdapter->ANum);

	return (0);
}

static void diva_bri_clear_interrupts(diva_os_xdi_adapter_t * a)
{
	PISDN_ADAPTER IoAdapter = &a->xdi_adapter;

	/*
	   clear any pending interrupt
	 */
	IoAdapter->disIrq(IoAdapter);

	IoAdapter->tst_irq(&IoAdapter->a);
	IoAdapter->clr_irq(&IoAdapter->a);
	IoAdapter->tst_irq(&IoAdapter->a);

	/*
	   kill pending dpcs
	 */
	diva_os_cancel_soft_isr(&IoAdapter->req_soft_isr);
	diva_os_cancel_soft_isr(&IoAdapter->isr_soft_isr);
}

/*
**  Stop card
*/
static int diva_bri_stop_adapter(diva_os_xdi_adapter_t * a)
{
	PISDN_ADAPTER IoAdapter = &a->xdi_adapter;
	int i = 100;

	if (!IoAdapter->port) {
		return (-1);
	}
	if (!IoAdapter->Initialized) {
		DBG_ERR(("A: A(%d) can't stop BRI adapter - not running",
			 IoAdapter->ANum))
		return (-1);	/* nothing to stop */
	}
	IoAdapter->Initialized = 0;

	/*
	   Disconnect Adapter from DIDD
	 */
	diva_xdi_didd_remove_adapter(IoAdapter->ANum);

	/*
	   Stop interrupts
	 */
	a->clear_interrupts_proc = diva_bri_clear_interrupts;
	IoAdapter->a.ReadyInt = 1;
	IoAdapter->a.ram_inc(&IoAdapter->a, &PR_RAM->ReadyInt);
	do {
		diva_os_sleep(10);
	} while (i-- && a->clear_interrupts_proc);
	if (a->clear_interrupts_proc) {
		diva_bri_clear_interrupts(a);
		a->clear_interrupts_proc = NULL;
		DBG_ERR(("A: A(%d) no final interrupt from BRI adapter",
			 IoAdapter->ANum))
	}
	IoAdapter->a.ReadyInt = 0;

	/*
	   Stop and reset adapter
	 */
	IoAdapter->stop(IoAdapter);

	return (0);
}
