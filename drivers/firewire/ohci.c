// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for OHCI 1394 controllers
 *
 * Copyright (C) 2003-2006 Kristian Hoegsberg <krh@bitplanet.net>
 */

#include <linux/bitops.h>
#include <linux/bug.h>
#include <linux/compiler.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/firewire.h>
#include <linux/firewire-constants.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/time.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>

#include <asm/byteorder.h>
#include <asm/page.h>

#ifdef CONFIG_PPC_PMAC
#include <asm/pmac_feature.h>
#endif

#include "core.h"
#include "ohci.h"

#define ohci_info(ohci, f, args...)	dev_info(ohci->card.device, f, ##args)
#define ohci_notice(ohci, f, args...)	dev_notice(ohci->card.device, f, ##args)
#define ohci_err(ohci, f, args...)	dev_err(ohci->card.device, f, ##args)

#define DESCRIPTOR_OUTPUT_MORE		0
#define DESCRIPTOR_OUTPUT_LAST		(1 << 12)
#define DESCRIPTOR_INPUT_MORE		(2 << 12)
#define DESCRIPTOR_INPUT_LAST		(3 << 12)
#define DESCRIPTOR_STATUS		(1 << 11)
#define DESCRIPTOR_KEY_IMMEDIATE	(2 << 8)
#define DESCRIPTOR_PING			(1 << 7)
#define DESCRIPTOR_YY			(1 << 6)
#define DESCRIPTOR_NO_IRQ		(0 << 4)
#define DESCRIPTOR_IRQ_ERROR		(1 << 4)
#define DESCRIPTOR_IRQ_ALWAYS		(3 << 4)
#define DESCRIPTOR_BRANCH_ALWAYS	(3 << 2)
#define DESCRIPTOR_WAIT			(3 << 0)

#define DESCRIPTOR_CMD			(0xf << 12)

struct descriptor {
	__le16 req_count;
	__le16 control;
	__le32 data_address;
	__le32 branch_address;
	__le16 res_count;
	__le16 transfer_status;
} __attribute__((aligned(16)));

#define CONTROL_SET(regs)	(regs)
#define CONTROL_CLEAR(regs)	((regs) + 4)
#define COMMAND_PTR(regs)	((regs) + 12)
#define CONTEXT_MATCH(regs)	((regs) + 16)

#define AR_BUFFER_SIZE	(32*1024)
#define AR_BUFFERS_MIN	DIV_ROUND_UP(AR_BUFFER_SIZE, PAGE_SIZE)
/* we need at least two pages for proper list management */
#define AR_BUFFERS	(AR_BUFFERS_MIN >= 2 ? AR_BUFFERS_MIN : 2)

#define MAX_ASYNC_PAYLOAD	4096
#define MAX_AR_PACKET_SIZE	(16 + MAX_ASYNC_PAYLOAD + 4)
#define AR_WRAPAROUND_PAGES	DIV_ROUND_UP(MAX_AR_PACKET_SIZE, PAGE_SIZE)

struct ar_context {
	struct fw_ohci *ohci;
	struct page *pages[AR_BUFFERS];
	void *buffer;
	struct descriptor *descriptors;
	dma_addr_t descriptors_bus;
	void *pointer;
	unsigned int last_buffer_index;
	u32 regs;
	struct tasklet_struct tasklet;
};

struct context;

typedef int (*descriptor_callback_t)(struct context *ctx,
				     struct descriptor *d,
				     struct descriptor *last);

/*
 * A buffer that contains a block of DMA-able coherent memory used for
 * storing a portion of a DMA descriptor program.
 */
struct descriptor_buffer {
	struct list_head list;
	dma_addr_t buffer_bus;
	size_t buffer_size;
	size_t used;
	struct descriptor buffer[];
};

struct context {
	struct fw_ohci *ohci;
	u32 regs;
	int total_allocation;
	u32 current_bus;
	bool running;
	bool flushing;

	/*
	 * List of page-sized buffers for storing DMA descriptors.
	 * Head of list contains buffers in use and tail of list contains
	 * free buffers.
	 */
	struct list_head buffer_list;

	/*
	 * Pointer to a buffer inside buffer_list that contains the tail
	 * end of the current DMA program.
	 */
	struct descriptor_buffer *buffer_tail;

	/*
	 * The descriptor containing the branch address of the first
	 * descriptor that has not yet been filled by the device.
	 */
	struct descriptor *last;

	/*
	 * The last descriptor block in the DMA program. It contains the branch
	 * address that must be updated upon appending a new descriptor.
	 */
	struct descriptor *prev;
	int prev_z;

	descriptor_callback_t callback;

	struct tasklet_struct tasklet;
};

#define IT_HEADER_SY(v)          ((v) <<  0)
#define IT_HEADER_TCODE(v)       ((v) <<  4)
#define IT_HEADER_CHANNEL(v)     ((v) <<  8)
#define IT_HEADER_TAG(v)         ((v) << 14)
#define IT_HEADER_SPEED(v)       ((v) << 16)
#define IT_HEADER_DATA_LENGTH(v) ((v) << 16)

struct iso_context {
	struct fw_iso_context base;
	struct context context;
	void *header;
	size_t header_length;
	unsigned long flushing_completions;
	u32 mc_buffer_bus;
	u16 mc_completed;
	u16 last_timestamp;
	u8 sync;
	u8 tags;
};

#define CONFIG_ROM_SIZE 1024

struct fw_ohci {
	struct fw_card card;

	__iomem char *registers;
	int node_id;
	int generation;
	int request_generation;	/* for timestamping incoming requests */
	unsigned quirks;
	unsigned int pri_req_max;
	u32 bus_time;
	bool bus_time_running;
	bool is_root;
	bool csr_state_setclear_abdicate;
	int n_ir;
	int n_it;
	/*
	 * Spinlock for accessing fw_ohci data.  Never call out of
	 * this driver with this lock held.
	 */
	spinlock_t lock;

	struct mutex phy_reg_mutex;

	void *misc_buffer;
	dma_addr_t misc_buffer_bus;

	struct ar_context ar_request_ctx;
	struct ar_context ar_response_ctx;
	struct context at_request_ctx;
	struct context at_response_ctx;

	u32 it_context_support;
	u32 it_context_mask;     /* unoccupied IT contexts */
	struct iso_context *it_context_list;
	u64 ir_context_channels; /* unoccupied channels */
	u32 ir_context_support;
	u32 ir_context_mask;     /* unoccupied IR contexts */
	struct iso_context *ir_context_list;
	u64 mc_channels; /* channels in use by the multichannel IR context */
	bool mc_allocated;

	__be32    *config_rom;
	dma_addr_t config_rom_bus;
	__be32    *next_config_rom;
	dma_addr_t next_config_rom_bus;
	__be32     next_header;

	__le32    *self_id;
	dma_addr_t self_id_bus;
	struct work_struct bus_reset_work;

	u32 self_id_buffer[512];
};

static struct workqueue_struct *selfid_workqueue;

static inline struct fw_ohci *fw_ohci(struct fw_card *card)
{
	return container_of(card, struct fw_ohci, card);
}

#define IT_CONTEXT_CYCLE_MATCH_ENABLE	0x80000000
#define IR_CONTEXT_BUFFER_FILL		0x80000000
#define IR_CONTEXT_ISOCH_HEADER		0x40000000
#define IR_CONTEXT_CYCLE_MATCH_ENABLE	0x20000000
#define IR_CONTEXT_MULTI_CHANNEL_MODE	0x10000000
#define IR_CONTEXT_DUAL_BUFFER_MODE	0x08000000

#define CONTEXT_RUN	0x8000
#define CONTEXT_WAKE	0x1000
#define CONTEXT_DEAD	0x0800
#define CONTEXT_ACTIVE	0x0400

#define OHCI1394_MAX_AT_REQ_RETRIES	0xf
#define OHCI1394_MAX_AT_RESP_RETRIES	0x2
#define OHCI1394_MAX_PHYS_RESP_RETRIES	0x8

#define OHCI1394_REGISTER_SIZE		0x800
#define OHCI1394_PCI_HCI_Control	0x40
#define SELF_ID_BUF_SIZE		0x800
#define OHCI_TCODE_PHY_PACKET		0x0e
#define OHCI_VERSION_1_1		0x010010

static char ohci_driver_name[] = KBUILD_MODNAME;

#define PCI_VENDOR_ID_PINNACLE_SYSTEMS	0x11bd
#define PCI_DEVICE_ID_AGERE_FW643	0x5901
#define PCI_DEVICE_ID_CREATIVE_SB1394	0x4001
#define PCI_DEVICE_ID_JMICRON_JMB38X_FW	0x2380
#define PCI_DEVICE_ID_TI_TSB12LV22	0x8009
#define PCI_DEVICE_ID_TI_TSB12LV26	0x8020
#define PCI_DEVICE_ID_TI_TSB82AA2	0x8025
#define PCI_DEVICE_ID_VIA_VT630X	0x3044
#define PCI_REV_ID_VIA_VT6306		0x46
#define PCI_DEVICE_ID_VIA_VT6315	0x3403

#define QUIRK_CYCLE_TIMER		0x1
#define QUIRK_RESET_PACKET		0x2
#define QUIRK_BE_HEADERS		0x4
#define QUIRK_NO_1394A			0x8
#define QUIRK_NO_MSI			0x10
#define QUIRK_TI_SLLZ059		0x20
#define QUIRK_IR_WAKE			0x40

// On PCI Express Root Complex in any type of AMD Ryzen machine, VIA VT6306/6307/6308 with Asmedia
// ASM1083/1085 brings an inconvenience that the read accesses to 'Isochronous Cycle Timer' register
// (at offset 0xf0 in PCI I/O space) often causes unexpected system reboot. The mechanism is not
// clear, since the read access to the other registers is enough safe; e.g. 'Node ID' register,
// while it is probable due to detection of any type of PCIe error.
#define QUIRK_REBOOT_BY_CYCLE_TIMER_READ	0x80000000

#if IS_ENABLED(CONFIG_X86)

static bool has_reboot_by_cycle_timer_read_quirk(const struct fw_ohci *ohci)
{
	return !!(ohci->quirks & QUIRK_REBOOT_BY_CYCLE_TIMER_READ);
}

#define PCI_DEVICE_ID_ASMEDIA_ASM108X	0x1080

static bool detect_vt630x_with_asm1083_on_amd_ryzen_machine(const struct pci_dev *pdev)
{
	const struct pci_dev *pcie_to_pci_bridge;

	// Detect any type of AMD Ryzen machine.
	if (!static_cpu_has(X86_FEATURE_ZEN))
		return false;

	// Detect VIA VT6306/6307/6308.
	if (pdev->vendor != PCI_VENDOR_ID_VIA)
		return false;
	if (pdev->device != PCI_DEVICE_ID_VIA_VT630X)
		return false;

	// Detect Asmedia ASM1083/1085.
	pcie_to_pci_bridge = pdev->bus->self;
	if (pcie_to_pci_bridge->vendor != PCI_VENDOR_ID_ASMEDIA)
		return false;
	if (pcie_to_pci_bridge->device != PCI_DEVICE_ID_ASMEDIA_ASM108X)
		return false;

	return true;
}

#else
#define has_reboot_by_cycle_timer_read_quirk(ohci) false
#define detect_vt630x_with_asm1083_on_amd_ryzen_machine(pdev)	false
#endif

/* In case of multiple matches in ohci_quirks[], only the first one is used. */
static const struct {
	unsigned short vendor, device, revision, flags;
} ohci_quirks[] = {
	{PCI_VENDOR_ID_AL, PCI_ANY_ID, PCI_ANY_ID,
		QUIRK_CYCLE_TIMER},

	{PCI_VENDOR_ID_APPLE, PCI_DEVICE_ID_APPLE_UNI_N_FW, PCI_ANY_ID,
		QUIRK_BE_HEADERS},

	{PCI_VENDOR_ID_ATT, PCI_DEVICE_ID_AGERE_FW643, 6,
		QUIRK_NO_MSI},

	{PCI_VENDOR_ID_CREATIVE, PCI_DEVICE_ID_CREATIVE_SB1394, PCI_ANY_ID,
		QUIRK_RESET_PACKET},

	{PCI_VENDOR_ID_JMICRON, PCI_DEVICE_ID_JMICRON_JMB38X_FW, PCI_ANY_ID,
		QUIRK_NO_MSI},

	{PCI_VENDOR_ID_NEC, PCI_ANY_ID, PCI_ANY_ID,
		QUIRK_CYCLE_TIMER},

	{PCI_VENDOR_ID_O2, PCI_ANY_ID, PCI_ANY_ID,
		QUIRK_NO_MSI},

	{PCI_VENDOR_ID_RICOH, PCI_ANY_ID, PCI_ANY_ID,
		QUIRK_CYCLE_TIMER | QUIRK_NO_MSI},

	{PCI_VENDOR_ID_TI, PCI_DEVICE_ID_TI_TSB12LV22, PCI_ANY_ID,
		QUIRK_CYCLE_TIMER | QUIRK_RESET_PACKET | QUIRK_NO_1394A},

	{PCI_VENDOR_ID_TI, PCI_DEVICE_ID_TI_TSB12LV26, PCI_ANY_ID,
		QUIRK_RESET_PACKET | QUIRK_TI_SLLZ059},

	{PCI_VENDOR_ID_TI, PCI_DEVICE_ID_TI_TSB82AA2, PCI_ANY_ID,
		QUIRK_RESET_PACKET | QUIRK_TI_SLLZ059},

	{PCI_VENDOR_ID_TI, PCI_ANY_ID, PCI_ANY_ID,
		QUIRK_RESET_PACKET},

	{PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_VT630X, PCI_REV_ID_VIA_VT6306,
		QUIRK_CYCLE_TIMER | QUIRK_IR_WAKE},

	{PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_VT6315, 0,
		QUIRK_CYCLE_TIMER /* FIXME: necessary? */ | QUIRK_NO_MSI},

	{PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_VT6315, PCI_ANY_ID,
		QUIRK_NO_MSI},

	{PCI_VENDOR_ID_VIA, PCI_ANY_ID, PCI_ANY_ID,
		QUIRK_CYCLE_TIMER | QUIRK_NO_MSI},
};

/* This overrides anything that was found in ohci_quirks[]. */
static int param_quirks;
module_param_named(quirks, param_quirks, int, 0644);
MODULE_PARM_DESC(quirks, "Chip quirks (default = 0"
	", nonatomic cycle timer = "	__stringify(QUIRK_CYCLE_TIMER)
	", reset packet generation = "	__stringify(QUIRK_RESET_PACKET)
	", AR/selfID endianness = "	__stringify(QUIRK_BE_HEADERS)
	", no 1394a enhancements = "	__stringify(QUIRK_NO_1394A)
	", disable MSI = "		__stringify(QUIRK_NO_MSI)
	", TI SLLZ059 erratum = "	__stringify(QUIRK_TI_SLLZ059)
	", IR wake unreliable = "	__stringify(QUIRK_IR_WAKE)
	")");

#define OHCI_PARAM_DEBUG_AT_AR		1
#define OHCI_PARAM_DEBUG_SELFIDS	2
#define OHCI_PARAM_DEBUG_IRQS		4
#define OHCI_PARAM_DEBUG_BUSRESETS	8 /* only effective before chip init */

static int param_debug;
module_param_named(debug, param_debug, int, 0644);
MODULE_PARM_DESC(debug, "Verbose logging (default = 0"
	", AT/AR events = "	__stringify(OHCI_PARAM_DEBUG_AT_AR)
	", self-IDs = "		__stringify(OHCI_PARAM_DEBUG_SELFIDS)
	", IRQs = "		__stringify(OHCI_PARAM_DEBUG_IRQS)
	", busReset events = "	__stringify(OHCI_PARAM_DEBUG_BUSRESETS)
	", or a combination, or all = -1)");

static bool param_remote_dma;
module_param_named(remote_dma, param_remote_dma, bool, 0444);
MODULE_PARM_DESC(remote_dma, "Enable unfiltered remote DMA (default = N)");

static void log_irqs(struct fw_ohci *ohci, u32 evt)
{
	if (likely(!(param_debug &
			(OHCI_PARAM_DEBUG_IRQS | OHCI_PARAM_DEBUG_BUSRESETS))))
		return;

	if (!(param_debug & OHCI_PARAM_DEBUG_IRQS) &&
	    !(evt & OHCI1394_busReset))
		return;

	ohci_notice(ohci, "IRQ %08x%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s\n", evt,
	    evt & OHCI1394_selfIDComplete	? " selfID"		: "",
	    evt & OHCI1394_RQPkt		? " AR_req"		: "",
	    evt & OHCI1394_RSPkt		? " AR_resp"		: "",
	    evt & OHCI1394_reqTxComplete	? " AT_req"		: "",
	    evt & OHCI1394_respTxComplete	? " AT_resp"		: "",
	    evt & OHCI1394_isochRx		? " IR"			: "",
	    evt & OHCI1394_isochTx		? " IT"			: "",
	    evt & OHCI1394_postedWriteErr	? " postedWriteErr"	: "",
	    evt & OHCI1394_cycleTooLong		? " cycleTooLong"	: "",
	    evt & OHCI1394_cycle64Seconds	? " cycle64Seconds"	: "",
	    evt & OHCI1394_cycleInconsistent	? " cycleInconsistent"	: "",
	    evt & OHCI1394_regAccessFail	? " regAccessFail"	: "",
	    evt & OHCI1394_unrecoverableError	? " unrecoverableError"	: "",
	    evt & OHCI1394_busReset		? " busReset"		: "",
	    evt & ~(OHCI1394_selfIDComplete | OHCI1394_RQPkt |
		    OHCI1394_RSPkt | OHCI1394_reqTxComplete |
		    OHCI1394_respTxComplete | OHCI1394_isochRx |
		    OHCI1394_isochTx | OHCI1394_postedWriteErr |
		    OHCI1394_cycleTooLong | OHCI1394_cycle64Seconds |
		    OHCI1394_cycleInconsistent |
		    OHCI1394_regAccessFail | OHCI1394_busReset)
						? " ?"			: "");
}

static const char *speed[] = {
	[0] = "S100", [1] = "S200", [2] = "S400",    [3] = "beta",
};
static const char *power[] = {
	[0] = "+0W",  [1] = "+15W", [2] = "+30W",    [3] = "+45W",
	[4] = "-3W",  [5] = " ?W",  [6] = "-3..-6W", [7] = "-3..-10W",
};
static const char port[] = { '.', '-', 'p', 'c', };

static char _p(u32 *s, int shift)
{
	return port[*s >> shift & 3];
}

static void log_selfids(struct fw_ohci *ohci, int generation, int self_id_count)
{
	u32 *s;

	if (likely(!(param_debug & OHCI_PARAM_DEBUG_SELFIDS)))
		return;

	ohci_notice(ohci, "%d selfIDs, generation %d, local node ID %04x\n",
		    self_id_count, generation, ohci->node_id);

	for (s = ohci->self_id_buffer; self_id_count--; ++s)
		if ((*s & 1 << 23) == 0)
			ohci_notice(ohci,
			    "selfID 0: %08x, phy %d [%c%c%c] %s gc=%d %s %s%s%s\n",
			    *s, *s >> 24 & 63, _p(s, 6), _p(s, 4), _p(s, 2),
			    speed[*s >> 14 & 3], *s >> 16 & 63,
			    power[*s >> 8 & 7], *s >> 22 & 1 ? "L" : "",
			    *s >> 11 & 1 ? "c" : "", *s & 2 ? "i" : "");
		else
			ohci_notice(ohci,
			    "selfID n: %08x, phy %d [%c%c%c%c%c%c%c%c]\n",
			    *s, *s >> 24 & 63,
			    _p(s, 16), _p(s, 14), _p(s, 12), _p(s, 10),
			    _p(s,  8), _p(s,  6), _p(s,  4), _p(s,  2));
}

static const char *evts[] = {
	[0x00] = "evt_no_status",	[0x01] = "-reserved-",
	[0x02] = "evt_long_packet",	[0x03] = "evt_missing_ack",
	[0x04] = "evt_underrun",	[0x05] = "evt_overrun",
	[0x06] = "evt_descriptor_read",	[0x07] = "evt_data_read",
	[0x08] = "evt_data_write",	[0x09] = "evt_bus_reset",
	[0x0a] = "evt_timeout",		[0x0b] = "evt_tcode_err",
	[0x0c] = "-reserved-",		[0x0d] = "-reserved-",
	[0x0e] = "evt_unknown",		[0x0f] = "evt_flushed",
	[0x10] = "-reserved-",		[0x11] = "ack_complete",
	[0x12] = "ack_pending ",	[0x13] = "-reserved-",
	[0x14] = "ack_busy_X",		[0x15] = "ack_busy_A",
	[0x16] = "ack_busy_B",		[0x17] = "-reserved-",
	[0x18] = "-reserved-",		[0x19] = "-reserved-",
	[0x1a] = "-reserved-",		[0x1b] = "ack_tardy",
	[0x1c] = "-reserved-",		[0x1d] = "ack_data_error",
	[0x1e] = "ack_type_error",	[0x1f] = "-reserved-",
	[0x20] = "pending/cancelled",
};
static const char *tcodes[] = {
	[0x0] = "QW req",		[0x1] = "BW req",
	[0x2] = "W resp",		[0x3] = "-reserved-",
	[0x4] = "QR req",		[0x5] = "BR req",
	[0x6] = "QR resp",		[0x7] = "BR resp",
	[0x8] = "cycle start",		[0x9] = "Lk req",
	[0xa] = "async stream packet",	[0xb] = "Lk resp",
	[0xc] = "-reserved-",		[0xd] = "-reserved-",
	[0xe] = "link internal",	[0xf] = "-reserved-",
};

static void log_ar_at_event(struct fw_ohci *ohci,
			    char dir, int speed, u32 *header, int evt)
{
	int tcode = header[0] >> 4 & 0xf;
	char specific[12];

	if (likely(!(param_debug & OHCI_PARAM_DEBUG_AT_AR)))
		return;

	if (unlikely(evt >= ARRAY_SIZE(evts)))
			evt = 0x1f;

	if (evt == OHCI1394_evt_bus_reset) {
		ohci_notice(ohci, "A%c evt_bus_reset, generation %d\n",
			    dir, (header[2] >> 16) & 0xff);
		return;
	}

	switch (tcode) {
	case 0x0: case 0x6: case 0x8:
		snprintf(specific, sizeof(specific), " = %08x",
			 be32_to_cpu((__force __be32)header[3]));
		break;
	case 0x1: case 0x5: case 0x7: case 0x9: case 0xb:
		snprintf(specific, sizeof(specific), " %x,%x",
			 header[3] >> 16, header[3] & 0xffff);
		break;
	default:
		specific[0] = '\0';
	}

	switch (tcode) {
	case 0xa:
		ohci_notice(ohci, "A%c %s, %s\n",
			    dir, evts[evt], tcodes[tcode]);
		break;
	case 0xe:
		ohci_notice(ohci, "A%c %s, PHY %08x %08x\n",
			    dir, evts[evt], header[1], header[2]);
		break;
	case 0x0: case 0x1: case 0x4: case 0x5: case 0x9:
		ohci_notice(ohci,
			    "A%c spd %x tl %02x, %04x -> %04x, %s, %s, %04x%08x%s\n",
			    dir, speed, header[0] >> 10 & 0x3f,
			    header[1] >> 16, header[0] >> 16, evts[evt],
			    tcodes[tcode], header[1] & 0xffff, header[2], specific);
		break;
	default:
		ohci_notice(ohci,
			    "A%c spd %x tl %02x, %04x -> %04x, %s, %s%s\n",
			    dir, speed, header[0] >> 10 & 0x3f,
			    header[1] >> 16, header[0] >> 16, evts[evt],
			    tcodes[tcode], specific);
	}
}

static inline void reg_write(const struct fw_ohci *ohci, int offset, u32 data)
{
	writel(data, ohci->registers + offset);
}

static inline u32 reg_read(const struct fw_ohci *ohci, int offset)
{
	return readl(ohci->registers + offset);
}

static inline void flush_writes(const struct fw_ohci *ohci)
{
	/* Do a dummy read to flush writes. */
	reg_read(ohci, OHCI1394_Version);
}

/*
 * Beware!  read_phy_reg(), write_phy_reg(), update_phy_reg(), and
 * read_paged_phy_reg() require the caller to hold ohci->phy_reg_mutex.
 * In other words, only use ohci_read_phy_reg() and ohci_update_phy_reg()
 * directly.  Exceptions are intrinsically serialized contexts like pci_probe.
 */
static int read_phy_reg(struct fw_ohci *ohci, int addr)
{
	u32 val;
	int i;

	reg_write(ohci, OHCI1394_PhyControl, OHCI1394_PhyControl_Read(addr));
	for (i = 0; i < 3 + 100; i++) {
		val = reg_read(ohci, OHCI1394_PhyControl);
		if (!~val)
			return -ENODEV; /* Card was ejected. */

		if (val & OHCI1394_PhyControl_ReadDone)
			return OHCI1394_PhyControl_ReadData(val);

		/*
		 * Try a few times without waiting.  Sleeping is necessary
		 * only when the link/PHY interface is busy.
		 */
		if (i >= 3)
			msleep(1);
	}
	ohci_err(ohci, "failed to read phy reg %d\n", addr);
	dump_stack();

	return -EBUSY;
}

static int write_phy_reg(const struct fw_ohci *ohci, int addr, u32 val)
{
	int i;

	reg_write(ohci, OHCI1394_PhyControl,
		  OHCI1394_PhyControl_Write(addr, val));
	for (i = 0; i < 3 + 100; i++) {
		val = reg_read(ohci, OHCI1394_PhyControl);
		if (!~val)
			return -ENODEV; /* Card was ejected. */

		if (!(val & OHCI1394_PhyControl_WritePending))
			return 0;

		if (i >= 3)
			msleep(1);
	}
	ohci_err(ohci, "failed to write phy reg %d, val %u\n", addr, val);
	dump_stack();

	return -EBUSY;
}

static int update_phy_reg(struct fw_ohci *ohci, int addr,
			  int clear_bits, int set_bits)
{
	int ret = read_phy_reg(ohci, addr);
	if (ret < 0)
		return ret;

	/*
	 * The interrupt status bits are cleared by writing a one bit.
	 * Avoid clearing them unless explicitly requested in set_bits.
	 */
	if (addr == 5)
		clear_bits |= PHY_INT_STATUS_BITS;

	return write_phy_reg(ohci, addr, (ret & ~clear_bits) | set_bits);
}

static int read_paged_phy_reg(struct fw_ohci *ohci, int page, int addr)
{
	int ret;

	ret = update_phy_reg(ohci, 7, PHY_PAGE_SELECT, page << 5);
	if (ret < 0)
		return ret;

	return read_phy_reg(ohci, addr);
}

static int ohci_read_phy_reg(struct fw_card *card, int addr)
{
	struct fw_ohci *ohci = fw_ohci(card);
	int ret;

	mutex_lock(&ohci->phy_reg_mutex);
	ret = read_phy_reg(ohci, addr);
	mutex_unlock(&ohci->phy_reg_mutex);

	return ret;
}

static int ohci_update_phy_reg(struct fw_card *card, int addr,
			       int clear_bits, int set_bits)
{
	struct fw_ohci *ohci = fw_ohci(card);
	int ret;

	mutex_lock(&ohci->phy_reg_mutex);
	ret = update_phy_reg(ohci, addr, clear_bits, set_bits);
	mutex_unlock(&ohci->phy_reg_mutex);

	return ret;
}

static inline dma_addr_t ar_buffer_bus(struct ar_context *ctx, unsigned int i)
{
	return page_private(ctx->pages[i]);
}

static void ar_context_link_page(struct ar_context *ctx, unsigned int index)
{
	struct descriptor *d;

	d = &ctx->descriptors[index];
	d->branch_address  &= cpu_to_le32(~0xf);
	d->res_count       =  cpu_to_le16(PAGE_SIZE);
	d->transfer_status =  0;

	wmb(); /* finish init of new descriptors before branch_address update */
	d = &ctx->descriptors[ctx->last_buffer_index];
	d->branch_address  |= cpu_to_le32(1);

	ctx->last_buffer_index = index;

	reg_write(ctx->ohci, CONTROL_SET(ctx->regs), CONTEXT_WAKE);
}

static void ar_context_release(struct ar_context *ctx)
{
	struct device *dev = ctx->ohci->card.device;
	unsigned int i;

	if (!ctx->buffer)
		return;

	vunmap(ctx->buffer);

	for (i = 0; i < AR_BUFFERS; i++) {
		if (ctx->pages[i])
			dma_free_pages(dev, PAGE_SIZE, ctx->pages[i],
				       ar_buffer_bus(ctx, i), DMA_FROM_DEVICE);
	}
}

static void ar_context_abort(struct ar_context *ctx, const char *error_msg)
{
	struct fw_ohci *ohci = ctx->ohci;

	if (reg_read(ohci, CONTROL_CLEAR(ctx->regs)) & CONTEXT_RUN) {
		reg_write(ohci, CONTROL_CLEAR(ctx->regs), CONTEXT_RUN);
		flush_writes(ohci);

		ohci_err(ohci, "AR error: %s; DMA stopped\n", error_msg);
	}
	/* FIXME: restart? */
}

static inline unsigned int ar_next_buffer_index(unsigned int index)
{
	return (index + 1) % AR_BUFFERS;
}

static inline unsigned int ar_first_buffer_index(struct ar_context *ctx)
{
	return ar_next_buffer_index(ctx->last_buffer_index);
}

/*
 * We search for the buffer that contains the last AR packet DMA data written
 * by the controller.
 */
static unsigned int ar_search_last_active_buffer(struct ar_context *ctx,
						 unsigned int *buffer_offset)
{
	unsigned int i, next_i, last = ctx->last_buffer_index;
	__le16 res_count, next_res_count;

	i = ar_first_buffer_index(ctx);
	res_count = READ_ONCE(ctx->descriptors[i].res_count);

	/* A buffer that is not yet completely filled must be the last one. */
	while (i != last && res_count == 0) {

		/* Peek at the next descriptor. */
		next_i = ar_next_buffer_index(i);
		rmb(); /* read descriptors in order */
		next_res_count = READ_ONCE(ctx->descriptors[next_i].res_count);
		/*
		 * If the next descriptor is still empty, we must stop at this
		 * descriptor.
		 */
		if (next_res_count == cpu_to_le16(PAGE_SIZE)) {
			/*
			 * The exception is when the DMA data for one packet is
			 * split over three buffers; in this case, the middle
			 * buffer's descriptor might be never updated by the
			 * controller and look still empty, and we have to peek
			 * at the third one.
			 */
			if (MAX_AR_PACKET_SIZE > PAGE_SIZE && i != last) {
				next_i = ar_next_buffer_index(next_i);
				rmb();
				next_res_count = READ_ONCE(ctx->descriptors[next_i].res_count);
				if (next_res_count != cpu_to_le16(PAGE_SIZE))
					goto next_buffer_is_active;
			}

			break;
		}

next_buffer_is_active:
		i = next_i;
		res_count = next_res_count;
	}

	rmb(); /* read res_count before the DMA data */

	*buffer_offset = PAGE_SIZE - le16_to_cpu(res_count);
	if (*buffer_offset > PAGE_SIZE) {
		*buffer_offset = 0;
		ar_context_abort(ctx, "corrupted descriptor");
	}

	return i;
}

static void ar_sync_buffers_for_cpu(struct ar_context *ctx,
				    unsigned int end_buffer_index,
				    unsigned int end_buffer_offset)
{
	unsigned int i;

	i = ar_first_buffer_index(ctx);
	while (i != end_buffer_index) {
		dma_sync_single_for_cpu(ctx->ohci->card.device,
					ar_buffer_bus(ctx, i),
					PAGE_SIZE, DMA_FROM_DEVICE);
		i = ar_next_buffer_index(i);
	}
	if (end_buffer_offset > 0)
		dma_sync_single_for_cpu(ctx->ohci->card.device,
					ar_buffer_bus(ctx, i),
					end_buffer_offset, DMA_FROM_DEVICE);
}

#if defined(CONFIG_PPC_PMAC) && defined(CONFIG_PPC32)
#define cond_le32_to_cpu(v) \
	(ohci->quirks & QUIRK_BE_HEADERS ? (__force __u32)(v) : le32_to_cpu(v))
#else
#define cond_le32_to_cpu(v) le32_to_cpu(v)
#endif

static __le32 *handle_ar_packet(struct ar_context *ctx, __le32 *buffer)
{
	struct fw_ohci *ohci = ctx->ohci;
	struct fw_packet p;
	u32 status, length, tcode;
	int evt;

	p.header[0] = cond_le32_to_cpu(buffer[0]);
	p.header[1] = cond_le32_to_cpu(buffer[1]);
	p.header[2] = cond_le32_to_cpu(buffer[2]);

	tcode = (p.header[0] >> 4) & 0x0f;
	switch (tcode) {
	case TCODE_WRITE_QUADLET_REQUEST:
	case TCODE_READ_QUADLET_RESPONSE:
		p.header[3] = (__force __u32) buffer[3];
		p.header_length = 16;
		p.payload_length = 0;
		break;

	case TCODE_READ_BLOCK_REQUEST :
		p.header[3] = cond_le32_to_cpu(buffer[3]);
		p.header_length = 16;
		p.payload_length = 0;
		break;

	case TCODE_WRITE_BLOCK_REQUEST:
	case TCODE_READ_BLOCK_RESPONSE:
	case TCODE_LOCK_REQUEST:
	case TCODE_LOCK_RESPONSE:
		p.header[3] = cond_le32_to_cpu(buffer[3]);
		p.header_length = 16;
		p.payload_length = p.header[3] >> 16;
		if (p.payload_length > MAX_ASYNC_PAYLOAD) {
			ar_context_abort(ctx, "invalid packet length");
			return NULL;
		}
		break;

	case TCODE_WRITE_RESPONSE:
	case TCODE_READ_QUADLET_REQUEST:
	case OHCI_TCODE_PHY_PACKET:
		p.header_length = 12;
		p.payload_length = 0;
		break;

	default:
		ar_context_abort(ctx, "invalid tcode");
		return NULL;
	}

	p.payload = (void *) buffer + p.header_length;

	/* FIXME: What to do about evt_* errors? */
	length = (p.header_length + p.payload_length + 3) / 4;
	status = cond_le32_to_cpu(buffer[length]);
	evt    = (status >> 16) & 0x1f;

	p.ack        = evt - 16;
	p.speed      = (status >> 21) & 0x7;
	p.timestamp  = status & 0xffff;
	p.generation = ohci->request_generation;

	log_ar_at_event(ohci, 'R', p.speed, p.header, evt);

	/*
	 * Several controllers, notably from NEC and VIA, forget to
	 * write ack_complete status at PHY packet reception.
	 */
	if (evt == OHCI1394_evt_no_status &&
	    (p.header[0] & 0xff) == (OHCI1394_phy_tcode << 4))
		p.ack = ACK_COMPLETE;

	/*
	 * The OHCI bus reset handler synthesizes a PHY packet with
	 * the new generation number when a bus reset happens (see
	 * section 8.4.2.3).  This helps us determine when a request
	 * was received and make sure we send the response in the same
	 * generation.  We only need this for requests; for responses
	 * we use the unique tlabel for finding the matching
	 * request.
	 *
	 * Alas some chips sometimes emit bus reset packets with a
	 * wrong generation.  We set the correct generation for these
	 * at a slightly incorrect time (in bus_reset_work).
	 */
	if (evt == OHCI1394_evt_bus_reset) {
		if (!(ohci->quirks & QUIRK_RESET_PACKET))
			ohci->request_generation = (p.header[2] >> 16) & 0xff;
	} else if (ctx == &ohci->ar_request_ctx) {
		fw_core_handle_request(&ohci->card, &p);
	} else {
		fw_core_handle_response(&ohci->card, &p);
	}

	return buffer + length + 1;
}

static void *handle_ar_packets(struct ar_context *ctx, void *p, void *end)
{
	void *next;

	while (p < end) {
		next = handle_ar_packet(ctx, p);
		if (!next)
			return p;
		p = next;
	}

	return p;
}

static void ar_recycle_buffers(struct ar_context *ctx, unsigned int end_buffer)
{
	unsigned int i;

	i = ar_first_buffer_index(ctx);
	while (i != end_buffer) {
		dma_sync_single_for_device(ctx->ohci->card.device,
					   ar_buffer_bus(ctx, i),
					   PAGE_SIZE, DMA_FROM_DEVICE);
		ar_context_link_page(ctx, i);
		i = ar_next_buffer_index(i);
	}
}

static void ar_context_tasklet(unsigned long data)
{
	struct ar_context *ctx = (struct ar_context *)data;
	unsigned int end_buffer_index, end_buffer_offset;
	void *p, *end;

	p = ctx->pointer;
	if (!p)
		return;

	end_buffer_index = ar_search_last_active_buffer(ctx,
							&end_buffer_offset);
	ar_sync_buffers_for_cpu(ctx, end_buffer_index, end_buffer_offset);
	end = ctx->buffer + end_buffer_index * PAGE_SIZE + end_buffer_offset;

	if (end_buffer_index < ar_first_buffer_index(ctx)) {
		/*
		 * The filled part of the overall buffer wraps around; handle
		 * all packets up to the buffer end here.  If the last packet
		 * wraps around, its tail will be visible after the buffer end
		 * because the buffer start pages are mapped there again.
		 */
		void *buffer_end = ctx->buffer + AR_BUFFERS * PAGE_SIZE;
		p = handle_ar_packets(ctx, p, buffer_end);
		if (p < buffer_end)
			goto error;
		/* adjust p to point back into the actual buffer */
		p -= AR_BUFFERS * PAGE_SIZE;
	}

	p = handle_ar_packets(ctx, p, end);
	if (p != end) {
		if (p > end)
			ar_context_abort(ctx, "inconsistent descriptor");
		goto error;
	}

	ctx->pointer = p;
	ar_recycle_buffers(ctx, end_buffer_index);

	return;

error:
	ctx->pointer = NULL;
}

static int ar_context_init(struct ar_context *ctx, struct fw_ohci *ohci,
			   unsigned int descriptors_offset, u32 regs)
{
	struct device *dev = ohci->card.device;
	unsigned int i;
	dma_addr_t dma_addr;
	struct page *pages[AR_BUFFERS + AR_WRAPAROUND_PAGES];
	struct descriptor *d;

	ctx->regs        = regs;
	ctx->ohci        = ohci;
	tasklet_init(&ctx->tasklet, ar_context_tasklet, (unsigned long)ctx);

	for (i = 0; i < AR_BUFFERS; i++) {
		ctx->pages[i] = dma_alloc_pages(dev, PAGE_SIZE, &dma_addr,
						DMA_FROM_DEVICE, GFP_KERNEL);
		if (!ctx->pages[i])
			goto out_of_memory;
		set_page_private(ctx->pages[i], dma_addr);
		dma_sync_single_for_device(dev, dma_addr, PAGE_SIZE,
					   DMA_FROM_DEVICE);
	}

	for (i = 0; i < AR_BUFFERS; i++)
		pages[i]              = ctx->pages[i];
	for (i = 0; i < AR_WRAPAROUND_PAGES; i++)
		pages[AR_BUFFERS + i] = ctx->pages[i];
	ctx->buffer = vmap(pages, ARRAY_SIZE(pages), VM_MAP, PAGE_KERNEL);
	if (!ctx->buffer)
		goto out_of_memory;

	ctx->descriptors     = ohci->misc_buffer     + descriptors_offset;
	ctx->descriptors_bus = ohci->misc_buffer_bus + descriptors_offset;

	for (i = 0; i < AR_BUFFERS; i++) {
		d = &ctx->descriptors[i];
		d->req_count      = cpu_to_le16(PAGE_SIZE);
		d->control        = cpu_to_le16(DESCRIPTOR_INPUT_MORE |
						DESCRIPTOR_STATUS |
						DESCRIPTOR_BRANCH_ALWAYS);
		d->data_address   = cpu_to_le32(ar_buffer_bus(ctx, i));
		d->branch_address = cpu_to_le32(ctx->descriptors_bus +
			ar_next_buffer_index(i) * sizeof(struct descriptor));
	}

	return 0;

out_of_memory:
	ar_context_release(ctx);

	return -ENOMEM;
}

static void ar_context_run(struct ar_context *ctx)
{
	unsigned int i;

	for (i = 0; i < AR_BUFFERS; i++)
		ar_context_link_page(ctx, i);

	ctx->pointer = ctx->buffer;

	reg_write(ctx->ohci, COMMAND_PTR(ctx->regs), ctx->descriptors_bus | 1);
	reg_write(ctx->ohci, CONTROL_SET(ctx->regs), CONTEXT_RUN);
}

static struct descriptor *find_branch_descriptor(struct descriptor *d, int z)
{
	__le16 branch;

	branch = d->control & cpu_to_le16(DESCRIPTOR_BRANCH_ALWAYS);

	/* figure out which descriptor the branch address goes in */
	if (z == 2 && branch == cpu_to_le16(DESCRIPTOR_BRANCH_ALWAYS))
		return d;
	else
		return d + z - 1;
}

static void context_tasklet(unsigned long data)
{
	struct context *ctx = (struct context *) data;
	struct descriptor *d, *last;
	u32 address;
	int z;
	struct descriptor_buffer *desc;

	desc = list_entry(ctx->buffer_list.next,
			struct descriptor_buffer, list);
	last = ctx->last;
	while (last->branch_address != 0) {
		struct descriptor_buffer *old_desc = desc;
		address = le32_to_cpu(last->branch_address);
		z = address & 0xf;
		address &= ~0xf;
		ctx->current_bus = address;

		/* If the branch address points to a buffer outside of the
		 * current buffer, advance to the next buffer. */
		if (address < desc->buffer_bus ||
				address >= desc->buffer_bus + desc->used)
			desc = list_entry(desc->list.next,
					struct descriptor_buffer, list);
		d = desc->buffer + (address - desc->buffer_bus) / sizeof(*d);
		last = find_branch_descriptor(d, z);

		if (!ctx->callback(ctx, d, last))
			break;

		if (old_desc != desc) {
			/* If we've advanced to the next buffer, move the
			 * previous buffer to the free list. */
			unsigned long flags;
			old_desc->used = 0;
			spin_lock_irqsave(&ctx->ohci->lock, flags);
			list_move_tail(&old_desc->list, &ctx->buffer_list);
			spin_unlock_irqrestore(&ctx->ohci->lock, flags);
		}
		ctx->last = last;
	}
}

/*
 * Allocate a new buffer and add it to the list of free buffers for this
 * context.  Must be called with ohci->lock held.
 */
static int context_add_buffer(struct context *ctx)
{
	struct descriptor_buffer *desc;
	dma_addr_t bus_addr;
	int offset;

	/*
	 * 16MB of descriptors should be far more than enough for any DMA
	 * program.  This will catch run-away userspace or DoS attacks.
	 */
	if (ctx->total_allocation >= 16*1024*1024)
		return -ENOMEM;

	desc = dmam_alloc_coherent(ctx->ohci->card.device, PAGE_SIZE, &bus_addr, GFP_ATOMIC);
	if (!desc)
		return -ENOMEM;

	offset = (void *)&desc->buffer - (void *)desc;
	/*
	 * Some controllers, like JMicron ones, always issue 0x20-byte DMA reads
	 * for descriptors, even 0x10-byte ones. This can cause page faults when
	 * an IOMMU is in use and the oversized read crosses a page boundary.
	 * Work around this by always leaving at least 0x10 bytes of padding.
	 */
	desc->buffer_size = PAGE_SIZE - offset - 0x10;
	desc->buffer_bus = bus_addr + offset;
	desc->used = 0;

	list_add_tail(&desc->list, &ctx->buffer_list);
	ctx->total_allocation += PAGE_SIZE;

	return 0;
}

static int context_init(struct context *ctx, struct fw_ohci *ohci,
			u32 regs, descriptor_callback_t callback)
{
	ctx->ohci = ohci;
	ctx->regs = regs;
	ctx->total_allocation = 0;

	INIT_LIST_HEAD(&ctx->buffer_list);
	if (context_add_buffer(ctx) < 0)
		return -ENOMEM;

	ctx->buffer_tail = list_entry(ctx->buffer_list.next,
			struct descriptor_buffer, list);

	tasklet_init(&ctx->tasklet, context_tasklet, (unsigned long)ctx);
	ctx->callback = callback;

	/*
	 * We put a dummy descriptor in the buffer that has a NULL
	 * branch address and looks like it's been sent.  That way we
	 * have a descriptor to append DMA programs to.
	 */
	memset(ctx->buffer_tail->buffer, 0, sizeof(*ctx->buffer_tail->buffer));
	ctx->buffer_tail->buffer->control = cpu_to_le16(DESCRIPTOR_OUTPUT_LAST);
	ctx->buffer_tail->buffer->transfer_status = cpu_to_le16(0x8011);
	ctx->buffer_tail->used += sizeof(*ctx->buffer_tail->buffer);
	ctx->last = ctx->buffer_tail->buffer;
	ctx->prev = ctx->buffer_tail->buffer;
	ctx->prev_z = 1;

	return 0;
}

static void context_release(struct context *ctx)
{
	struct fw_card *card = &ctx->ohci->card;
	struct descriptor_buffer *desc, *tmp;

	list_for_each_entry_safe(desc, tmp, &ctx->buffer_list, list) {
		dmam_free_coherent(card->device, PAGE_SIZE, desc,
				   desc->buffer_bus - ((void *)&desc->buffer - (void *)desc));
	}
}

/* Must be called with ohci->lock held */
static struct descriptor *context_get_descriptors(struct context *ctx,
						  int z, dma_addr_t *d_bus)
{
	struct descriptor *d = NULL;
	struct descriptor_buffer *desc = ctx->buffer_tail;

	if (z * sizeof(*d) > desc->buffer_size)
		return NULL;

	if (z * sizeof(*d) > desc->buffer_size - desc->used) {
		/* No room for the descriptor in this buffer, so advance to the
		 * next one. */

		if (desc->list.next == &ctx->buffer_list) {
			/* If there is no free buffer next in the list,
			 * allocate one. */
			if (context_add_buffer(ctx) < 0)
				return NULL;
		}
		desc = list_entry(desc->list.next,
				struct descriptor_buffer, list);
		ctx->buffer_tail = desc;
	}

	d = desc->buffer + desc->used / sizeof(*d);
	memset(d, 0, z * sizeof(*d));
	*d_bus = desc->buffer_bus + desc->used;

	return d;
}

static void context_run(struct context *ctx, u32 extra)
{
	struct fw_ohci *ohci = ctx->ohci;

	reg_write(ohci, COMMAND_PTR(ctx->regs),
		  le32_to_cpu(ctx->last->branch_address));
	reg_write(ohci, CONTROL_CLEAR(ctx->regs), ~0);
	reg_write(ohci, CONTROL_SET(ctx->regs), CONTEXT_RUN | extra);
	ctx->running = true;
	flush_writes(ohci);
}

static void context_append(struct context *ctx,
			   struct descriptor *d, int z, int extra)
{
	dma_addr_t d_bus;
	struct descriptor_buffer *desc = ctx->buffer_tail;
	struct descriptor *d_branch;

	d_bus = desc->buffer_bus + (d - desc->buffer) * sizeof(*d);

	desc->used += (z + extra) * sizeof(*d);

	wmb(); /* finish init of new descriptors before branch_address update */

	d_branch = find_branch_descriptor(ctx->prev, ctx->prev_z);
	d_branch->branch_address = cpu_to_le32(d_bus | z);

	/*
	 * VT6306 incorrectly checks only the single descriptor at the
	 * CommandPtr when the wake bit is written, so if it's a
	 * multi-descriptor block starting with an INPUT_MORE, put a copy of
	 * the branch address in the first descriptor.
	 *
	 * Not doing this for transmit contexts since not sure how it interacts
	 * with skip addresses.
	 */
	if (unlikely(ctx->ohci->quirks & QUIRK_IR_WAKE) &&
	    d_branch != ctx->prev &&
	    (ctx->prev->control & cpu_to_le16(DESCRIPTOR_CMD)) ==
	     cpu_to_le16(DESCRIPTOR_INPUT_MORE)) {
		ctx->prev->branch_address = cpu_to_le32(d_bus | z);
	}

	ctx->prev = d;
	ctx->prev_z = z;
}

static void context_stop(struct context *ctx)
{
	struct fw_ohci *ohci = ctx->ohci;
	u32 reg;
	int i;

	reg_write(ohci, CONTROL_CLEAR(ctx->regs), CONTEXT_RUN);
	ctx->running = false;

	for (i = 0; i < 1000; i++) {
		reg = reg_read(ohci, CONTROL_SET(ctx->regs));
		if ((reg & CONTEXT_ACTIVE) == 0)
			return;

		if (i)
			udelay(10);
	}
	ohci_err(ohci, "DMA context still active (0x%08x)\n", reg);
}

struct driver_data {
	u8 inline_data[8];
	struct fw_packet *packet;
};

/*
 * This function apppends a packet to the DMA queue for transmission.
 * Must always be called with the ochi->lock held to ensure proper
 * generation handling and locking around packet queue manipulation.
 */
static int at_context_queue_packet(struct context *ctx,
				   struct fw_packet *packet)
{
	struct fw_ohci *ohci = ctx->ohci;
	dma_addr_t d_bus, payload_bus;
	struct driver_data *driver_data;
	struct descriptor *d, *last;
	__le32 *header;
	int z, tcode;

	d = context_get_descriptors(ctx, 4, &d_bus);
	if (d == NULL) {
		packet->ack = RCODE_SEND_ERROR;
		return -1;
	}

	d[0].control   = cpu_to_le16(DESCRIPTOR_KEY_IMMEDIATE);
	d[0].res_count = cpu_to_le16(packet->timestamp);

	/*
	 * The DMA format for asynchronous link packets is different
	 * from the IEEE1394 layout, so shift the fields around
	 * accordingly.
	 */

	tcode = (packet->header[0] >> 4) & 0x0f;
	header = (__le32 *) &d[1];
	switch (tcode) {
	case TCODE_WRITE_QUADLET_REQUEST:
	case TCODE_WRITE_BLOCK_REQUEST:
	case TCODE_WRITE_RESPONSE:
	case TCODE_READ_QUADLET_REQUEST:
	case TCODE_READ_BLOCK_REQUEST:
	case TCODE_READ_QUADLET_RESPONSE:
	case TCODE_READ_BLOCK_RESPONSE:
	case TCODE_LOCK_REQUEST:
	case TCODE_LOCK_RESPONSE:
		header[0] = cpu_to_le32((packet->header[0] & 0xffff) |
					(packet->speed << 16));
		header[1] = cpu_to_le32((packet->header[1] & 0xffff) |
					(packet->header[0] & 0xffff0000));
		header[2] = cpu_to_le32(packet->header[2]);

		if (TCODE_IS_BLOCK_PACKET(tcode))
			header[3] = cpu_to_le32(packet->header[3]);
		else
			header[3] = (__force __le32) packet->header[3];

		d[0].req_count = cpu_to_le16(packet->header_length);
		break;

	case TCODE_LINK_INTERNAL:
		header[0] = cpu_to_le32((OHCI1394_phy_tcode << 4) |
					(packet->speed << 16));
		header[1] = cpu_to_le32(packet->header[1]);
		header[2] = cpu_to_le32(packet->header[2]);
		d[0].req_count = cpu_to_le16(12);

		if (is_ping_packet(&packet->header[1]))
			d[0].control |= cpu_to_le16(DESCRIPTOR_PING);
		break;

	case TCODE_STREAM_DATA:
		header[0] = cpu_to_le32((packet->header[0] & 0xffff) |
					(packet->speed << 16));
		header[1] = cpu_to_le32(packet->header[0] & 0xffff0000);
		d[0].req_count = cpu_to_le16(8);
		break;

	default:
		/* BUG(); */
		packet->ack = RCODE_SEND_ERROR;
		return -1;
	}

	BUILD_BUG_ON(sizeof(struct driver_data) > sizeof(struct descriptor));
	driver_data = (struct driver_data *) &d[3];
	driver_data->packet = packet;
	packet->driver_data = driver_data;

	if (packet->payload_length > 0) {
		if (packet->payload_length > sizeof(driver_data->inline_data)) {
			payload_bus = dma_map_single(ohci->card.device,
						     packet->payload,
						     packet->payload_length,
						     DMA_TO_DEVICE);
			if (dma_mapping_error(ohci->card.device, payload_bus)) {
				packet->ack = RCODE_SEND_ERROR;
				return -1;
			}
			packet->payload_bus	= payload_bus;
			packet->payload_mapped	= true;
		} else {
			memcpy(driver_data->inline_data, packet->payload,
			       packet->payload_length);
			payload_bus = d_bus + 3 * sizeof(*d);
		}

		d[2].req_count    = cpu_to_le16(packet->payload_length);
		d[2].data_address = cpu_to_le32(payload_bus);
		last = &d[2];
		z = 3;
	} else {
		last = &d[0];
		z = 2;
	}

	last->control |= cpu_to_le16(DESCRIPTOR_OUTPUT_LAST |
				     DESCRIPTOR_IRQ_ALWAYS |
				     DESCRIPTOR_BRANCH_ALWAYS);

	/* FIXME: Document how the locking works. */
	if (ohci->generation != packet->generation) {
		if (packet->payload_mapped)
			dma_unmap_single(ohci->card.device, payload_bus,
					 packet->payload_length, DMA_TO_DEVICE);
		packet->ack = RCODE_GENERATION;
		return -1;
	}

	context_append(ctx, d, z, 4 - z);

	if (ctx->running)
		reg_write(ohci, CONTROL_SET(ctx->regs), CONTEXT_WAKE);
	else
		context_run(ctx, 0);

	return 0;
}

static void at_context_flush(struct context *ctx)
{
	tasklet_disable(&ctx->tasklet);

	ctx->flushing = true;
	context_tasklet((unsigned long)ctx);
	ctx->flushing = false;

	tasklet_enable(&ctx->tasklet);
}

static int handle_at_packet(struct context *context,
			    struct descriptor *d,
			    struct descriptor *last)
{
	struct driver_data *driver_data;
	struct fw_packet *packet;
	struct fw_ohci *ohci = context->ohci;
	int evt;

	if (last->transfer_status == 0 && !context->flushing)
		/* This descriptor isn't done yet, stop iteration. */
		return 0;

	driver_data = (struct driver_data *) &d[3];
	packet = driver_data->packet;
	if (packet == NULL)
		/* This packet was cancelled, just continue. */
		return 1;

	if (packet->payload_mapped)
		dma_unmap_single(ohci->card.device, packet->payload_bus,
				 packet->payload_length, DMA_TO_DEVICE);

	evt = le16_to_cpu(last->transfer_status) & 0x1f;
	packet->timestamp = le16_to_cpu(last->res_count);

	log_ar_at_event(ohci, 'T', packet->speed, packet->header, evt);

	switch (evt) {
	case OHCI1394_evt_timeout:
		/* Async response transmit timed out. */
		packet->ack = RCODE_CANCELLED;
		break;

	case OHCI1394_evt_flushed:
		/*
		 * The packet was flushed should give same error as
		 * when we try to use a stale generation count.
		 */
		packet->ack = RCODE_GENERATION;
		break;

	case OHCI1394_evt_missing_ack:
		if (context->flushing)
			packet->ack = RCODE_GENERATION;
		else {
			/*
			 * Using a valid (current) generation count, but the
			 * node is not on the bus or not sending acks.
			 */
			packet->ack = RCODE_NO_ACK;
		}
		break;

	case ACK_COMPLETE + 0x10:
	case ACK_PENDING + 0x10:
	case ACK_BUSY_X + 0x10:
	case ACK_BUSY_A + 0x10:
	case ACK_BUSY_B + 0x10:
	case ACK_DATA_ERROR + 0x10:
	case ACK_TYPE_ERROR + 0x10:
		packet->ack = evt - 0x10;
		break;

	case OHCI1394_evt_no_status:
		if (context->flushing) {
			packet->ack = RCODE_GENERATION;
			break;
		}
		fallthrough;

	default:
		packet->ack = RCODE_SEND_ERROR;
		break;
	}

	packet->callback(packet, &ohci->card, packet->ack);

	return 1;
}

#define HEADER_GET_DESTINATION(q)	(((q) >> 16) & 0xffff)
#define HEADER_GET_TCODE(q)		(((q) >> 4) & 0x0f)
#define HEADER_GET_OFFSET_HIGH(q)	(((q) >> 0) & 0xffff)
#define HEADER_GET_DATA_LENGTH(q)	(((q) >> 16) & 0xffff)
#define HEADER_GET_EXTENDED_TCODE(q)	(((q) >> 0) & 0xffff)

static u32 get_cycle_time(struct fw_ohci *ohci);

static void handle_local_rom(struct fw_ohci *ohci,
			     struct fw_packet *packet, u32 csr)
{
	struct fw_packet response;
	int tcode, length, i;

	tcode = HEADER_GET_TCODE(packet->header[0]);
	if (TCODE_IS_BLOCK_PACKET(tcode))
		length = HEADER_GET_DATA_LENGTH(packet->header[3]);
	else
		length = 4;

	i = csr - CSR_CONFIG_ROM;
	if (i + length > CONFIG_ROM_SIZE) {
		fw_fill_response(&response, packet->header,
				 RCODE_ADDRESS_ERROR, NULL, 0);
	} else if (!TCODE_IS_READ_REQUEST(tcode)) {
		fw_fill_response(&response, packet->header,
				 RCODE_TYPE_ERROR, NULL, 0);
	} else {
		fw_fill_response(&response, packet->header, RCODE_COMPLETE,
				 (void *) ohci->config_rom + i, length);
	}

	// Timestamping on behalf of the hardware.
	response.timestamp = cycle_time_to_ohci_tstamp(get_cycle_time(ohci));
	fw_core_handle_response(&ohci->card, &response);
}

static void handle_local_lock(struct fw_ohci *ohci,
			      struct fw_packet *packet, u32 csr)
{
	struct fw_packet response;
	int tcode, length, ext_tcode, sel, try;
	__be32 *payload, lock_old;
	u32 lock_arg, lock_data;

	tcode = HEADER_GET_TCODE(packet->header[0]);
	length = HEADER_GET_DATA_LENGTH(packet->header[3]);
	payload = packet->payload;
	ext_tcode = HEADER_GET_EXTENDED_TCODE(packet->header[3]);

	if (tcode == TCODE_LOCK_REQUEST &&
	    ext_tcode == EXTCODE_COMPARE_SWAP && length == 8) {
		lock_arg = be32_to_cpu(payload[0]);
		lock_data = be32_to_cpu(payload[1]);
	} else if (tcode == TCODE_READ_QUADLET_REQUEST) {
		lock_arg = 0;
		lock_data = 0;
	} else {
		fw_fill_response(&response, packet->header,
				 RCODE_TYPE_ERROR, NULL, 0);
		goto out;
	}

	sel = (csr - CSR_BUS_MANAGER_ID) / 4;
	reg_write(ohci, OHCI1394_CSRData, lock_data);
	reg_write(ohci, OHCI1394_CSRCompareData, lock_arg);
	reg_write(ohci, OHCI1394_CSRControl, sel);

	for (try = 0; try < 20; try++)
		if (reg_read(ohci, OHCI1394_CSRControl) & 0x80000000) {
			lock_old = cpu_to_be32(reg_read(ohci,
							OHCI1394_CSRData));
			fw_fill_response(&response, packet->header,
					 RCODE_COMPLETE,
					 &lock_old, sizeof(lock_old));
			goto out;
		}

	ohci_err(ohci, "swap not done (CSR lock timeout)\n");
	fw_fill_response(&response, packet->header, RCODE_BUSY, NULL, 0);

 out:
	// Timestamping on behalf of the hardware.
	response.timestamp = cycle_time_to_ohci_tstamp(get_cycle_time(ohci));
	fw_core_handle_response(&ohci->card, &response);
}

static void handle_local_request(struct context *ctx, struct fw_packet *packet)
{
	u64 offset, csr;

	if (ctx == &ctx->ohci->at_request_ctx) {
		packet->ack = ACK_PENDING;
		packet->callback(packet, &ctx->ohci->card, packet->ack);
	}

	offset =
		((unsigned long long)
		 HEADER_GET_OFFSET_HIGH(packet->header[1]) << 32) |
		packet->header[2];
	csr = offset - CSR_REGISTER_BASE;

	/* Handle config rom reads. */
	if (csr >= CSR_CONFIG_ROM && csr < CSR_CONFIG_ROM_END)
		handle_local_rom(ctx->ohci, packet, csr);
	else switch (csr) {
	case CSR_BUS_MANAGER_ID:
	case CSR_BANDWIDTH_AVAILABLE:
	case CSR_CHANNELS_AVAILABLE_HI:
	case CSR_CHANNELS_AVAILABLE_LO:
		handle_local_lock(ctx->ohci, packet, csr);
		break;
	default:
		if (ctx == &ctx->ohci->at_request_ctx)
			fw_core_handle_request(&ctx->ohci->card, packet);
		else
			fw_core_handle_response(&ctx->ohci->card, packet);
		break;
	}

	if (ctx == &ctx->ohci->at_response_ctx) {
		packet->ack = ACK_COMPLETE;
		packet->callback(packet, &ctx->ohci->card, packet->ack);
	}
}

static void at_context_transmit(struct context *ctx, struct fw_packet *packet)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&ctx->ohci->lock, flags);

	if (HEADER_GET_DESTINATION(packet->header[0]) == ctx->ohci->node_id &&
	    ctx->ohci->generation == packet->generation) {
		spin_unlock_irqrestore(&ctx->ohci->lock, flags);

		// Timestamping on behalf of the hardware.
		packet->timestamp = cycle_time_to_ohci_tstamp(get_cycle_time(ctx->ohci));

		handle_local_request(ctx, packet);
		return;
	}

	ret = at_context_queue_packet(ctx, packet);
	spin_unlock_irqrestore(&ctx->ohci->lock, flags);

	if (ret < 0) {
		// Timestamping on behalf of the hardware.
		packet->timestamp = cycle_time_to_ohci_tstamp(get_cycle_time(ctx->ohci));

		packet->callback(packet, &ctx->ohci->card, packet->ack);
	}
}

static void detect_dead_context(struct fw_ohci *ohci,
				const char *name, unsigned int regs)
{
	u32 ctl;

	ctl = reg_read(ohci, CONTROL_SET(regs));
	if (ctl & CONTEXT_DEAD)
		ohci_err(ohci, "DMA context %s has stopped, error code: %s\n",
			name, evts[ctl & 0x1f]);
}

static void handle_dead_contexts(struct fw_ohci *ohci)
{
	unsigned int i;
	char name[8];

	detect_dead_context(ohci, "ATReq", OHCI1394_AsReqTrContextBase);
	detect_dead_context(ohci, "ATRsp", OHCI1394_AsRspTrContextBase);
	detect_dead_context(ohci, "ARReq", OHCI1394_AsReqRcvContextBase);
	detect_dead_context(ohci, "ARRsp", OHCI1394_AsRspRcvContextBase);
	for (i = 0; i < 32; ++i) {
		if (!(ohci->it_context_support & (1 << i)))
			continue;
		sprintf(name, "IT%u", i);
		detect_dead_context(ohci, name, OHCI1394_IsoXmitContextBase(i));
	}
	for (i = 0; i < 32; ++i) {
		if (!(ohci->ir_context_support & (1 << i)))
			continue;
		sprintf(name, "IR%u", i);
		detect_dead_context(ohci, name, OHCI1394_IsoRcvContextBase(i));
	}
	/* TODO: maybe try to flush and restart the dead contexts */
}

static u32 cycle_timer_ticks(u32 cycle_timer)
{
	u32 ticks;

	ticks = cycle_timer & 0xfff;
	ticks += 3072 * ((cycle_timer >> 12) & 0x1fff);
	ticks += (3072 * 8000) * (cycle_timer >> 25);

	return ticks;
}

/*
 * Some controllers exhibit one or more of the following bugs when updating the
 * iso cycle timer register:
 *  - When the lowest six bits are wrapping around to zero, a read that happens
 *    at the same time will return garbage in the lowest ten bits.
 *  - When the cycleOffset field wraps around to zero, the cycleCount field is
 *    not incremented for about 60 ns.
 *  - Occasionally, the entire register reads zero.
 *
 * To catch these, we read the register three times and ensure that the
 * difference between each two consecutive reads is approximately the same, i.e.
 * less than twice the other.  Furthermore, any negative difference indicates an
 * error.  (A PCI read should take at least 20 ticks of the 24.576 MHz timer to
 * execute, so we have enough precision to compute the ratio of the differences.)
 */
static u32 get_cycle_time(struct fw_ohci *ohci)
{
	u32 c0, c1, c2;
	u32 t0, t1, t2;
	s32 diff01, diff12;
	int i;

	if (has_reboot_by_cycle_timer_read_quirk(ohci))
		return 0;

	c2 = reg_read(ohci, OHCI1394_IsochronousCycleTimer);

	if (ohci->quirks & QUIRK_CYCLE_TIMER) {
		i = 0;
		c1 = c2;
		c2 = reg_read(ohci, OHCI1394_IsochronousCycleTimer);
		do {
			c0 = c1;
			c1 = c2;
			c2 = reg_read(ohci, OHCI1394_IsochronousCycleTimer);
			t0 = cycle_timer_ticks(c0);
			t1 = cycle_timer_ticks(c1);
			t2 = cycle_timer_ticks(c2);
			diff01 = t1 - t0;
			diff12 = t2 - t1;
		} while ((diff01 <= 0 || diff12 <= 0 ||
			  diff01 / diff12 >= 2 || diff12 / diff01 >= 2)
			 && i++ < 20);
	}

	return c2;
}

/*
 * This function has to be called at least every 64 seconds.  The bus_time
 * field stores not only the upper 25 bits of the BUS_TIME register but also
 * the most significant bit of the cycle timer in bit 6 so that we can detect
 * changes in this bit.
 */
static u32 update_bus_time(struct fw_ohci *ohci)
{
	u32 cycle_time_seconds = get_cycle_time(ohci) >> 25;

	if (unlikely(!ohci->bus_time_running)) {
		reg_write(ohci, OHCI1394_IntMaskSet, OHCI1394_cycle64Seconds);
		ohci->bus_time = (lower_32_bits(ktime_get_seconds()) & ~0x7f) |
		                 (cycle_time_seconds & 0x40);
		ohci->bus_time_running = true;
	}

	if ((ohci->bus_time & 0x40) != (cycle_time_seconds & 0x40))
		ohci->bus_time += 0x40;

	return ohci->bus_time | cycle_time_seconds;
}

static int get_status_for_port(struct fw_ohci *ohci, int port_index)
{
	int reg;

	mutex_lock(&ohci->phy_reg_mutex);
	reg = write_phy_reg(ohci, 7, port_index);
	if (reg >= 0)
		reg = read_phy_reg(ohci, 8);
	mutex_unlock(&ohci->phy_reg_mutex);
	if (reg < 0)
		return reg;

	switch (reg & 0x0f) {
	case 0x06:
		return 2;	/* is child node (connected to parent node) */
	case 0x0e:
		return 3;	/* is parent node (connected to child node) */
	}
	return 1;		/* not connected */
}

static int get_self_id_pos(struct fw_ohci *ohci, u32 self_id,
	int self_id_count)
{
	int i;
	u32 entry;

	for (i = 0; i < self_id_count; i++) {
		entry = ohci->self_id_buffer[i];
		if ((self_id & 0xff000000) == (entry & 0xff000000))
			return -1;
		if ((self_id & 0xff000000) < (entry & 0xff000000))
			return i;
	}
	return i;
}

static int initiated_reset(struct fw_ohci *ohci)
{
	int reg;
	int ret = 0;

	mutex_lock(&ohci->phy_reg_mutex);
	reg = write_phy_reg(ohci, 7, 0xe0); /* Select page 7 */
	if (reg >= 0) {
		reg = read_phy_reg(ohci, 8);
		reg |= 0x40;
		reg = write_phy_reg(ohci, 8, reg); /* set PMODE bit */
		if (reg >= 0) {
			reg = read_phy_reg(ohci, 12); /* read register 12 */
			if (reg >= 0) {
				if ((reg & 0x08) == 0x08) {
					/* bit 3 indicates "initiated reset" */
					ret = 0x2;
				}
			}
		}
	}
	mutex_unlock(&ohci->phy_reg_mutex);
	return ret;
}

/*
 * TI TSB82AA2B and TSB12LV26 do not receive the selfID of a locally
 * attached TSB41BA3D phy; see http://www.ti.com/litv/pdf/sllz059.
 * Construct the selfID from phy register contents.
 */
static int find_and_insert_self_id(struct fw_ohci *ohci, int self_id_count)
{
	int reg, i, pos, status;
	/* link active 1, speed 3, bridge 0, contender 1, more packets 0 */
	u32 self_id = 0x8040c800;

	reg = reg_read(ohci, OHCI1394_NodeID);
	if (!(reg & OHCI1394_NodeID_idValid)) {
		ohci_notice(ohci,
			    "node ID not valid, new bus reset in progress\n");
		return -EBUSY;
	}
	self_id |= ((reg & 0x3f) << 24); /* phy ID */

	reg = ohci_read_phy_reg(&ohci->card, 4);
	if (reg < 0)
		return reg;
	self_id |= ((reg & 0x07) << 8); /* power class */

	reg = ohci_read_phy_reg(&ohci->card, 1);
	if (reg < 0)
		return reg;
	self_id |= ((reg & 0x3f) << 16); /* gap count */

	for (i = 0; i < 3; i++) {
		status = get_status_for_port(ohci, i);
		if (status < 0)
			return status;
		self_id |= ((status & 0x3) << (6 - (i * 2)));
	}

	self_id |= initiated_reset(ohci);

	pos = get_self_id_pos(ohci, self_id, self_id_count);
	if (pos >= 0) {
		memmove(&(ohci->self_id_buffer[pos+1]),
			&(ohci->self_id_buffer[pos]),
			(self_id_count - pos) * sizeof(*ohci->self_id_buffer));
		ohci->self_id_buffer[pos] = self_id;
		self_id_count++;
	}
	return self_id_count;
}

static void bus_reset_work(struct work_struct *work)
{
	struct fw_ohci *ohci =
		container_of(work, struct fw_ohci, bus_reset_work);
	int self_id_count, generation, new_generation, i, j;
	u32 reg;
	void *free_rom = NULL;
	dma_addr_t free_rom_bus = 0;
	bool is_new_root;

	reg = reg_read(ohci, OHCI1394_NodeID);
	if (!(reg & OHCI1394_NodeID_idValid)) {
		ohci_notice(ohci,
			    "node ID not valid, new bus reset in progress\n");
		return;
	}
	if ((reg & OHCI1394_NodeID_nodeNumber) == 63) {
		ohci_notice(ohci, "malconfigured bus\n");
		return;
	}
	ohci->node_id = reg & (OHCI1394_NodeID_busNumber |
			       OHCI1394_NodeID_nodeNumber);

	is_new_root = (reg & OHCI1394_NodeID_root) != 0;
	if (!(ohci->is_root && is_new_root))
		reg_write(ohci, OHCI1394_LinkControlSet,
			  OHCI1394_LinkControl_cycleMaster);
	ohci->is_root = is_new_root;

	reg = reg_read(ohci, OHCI1394_SelfIDCount);
	if (reg & OHCI1394_SelfIDCount_selfIDError) {
		ohci_notice(ohci, "self ID receive error\n");
		return;
	}
	/*
	 * The count in the SelfIDCount register is the number of
	 * bytes in the self ID receive buffer.  Since we also receive
	 * the inverted quadlets and a header quadlet, we shift one
	 * bit extra to get the actual number of self IDs.
	 */
	self_id_count = (reg >> 3) & 0xff;

	if (self_id_count > 252) {
		ohci_notice(ohci, "bad selfIDSize (%08x)\n", reg);
		return;
	}

	generation = (cond_le32_to_cpu(ohci->self_id[0]) >> 16) & 0xff;
	rmb();

	for (i = 1, j = 0; j < self_id_count; i += 2, j++) {
		u32 id  = cond_le32_to_cpu(ohci->self_id[i]);
		u32 id2 = cond_le32_to_cpu(ohci->self_id[i + 1]);

		if (id != ~id2) {
			/*
			 * If the invalid data looks like a cycle start packet,
			 * it's likely to be the result of the cycle master
			 * having a wrong gap count.  In this case, the self IDs
			 * so far are valid and should be processed so that the
			 * bus manager can then correct the gap count.
			 */
			if (id == 0xffff008f) {
				ohci_notice(ohci, "ignoring spurious self IDs\n");
				self_id_count = j;
				break;
			}

			ohci_notice(ohci, "bad self ID %d/%d (%08x != ~%08x)\n",
				    j, self_id_count, id, id2);
			return;
		}
		ohci->self_id_buffer[j] = id;
	}

	if (ohci->quirks & QUIRK_TI_SLLZ059) {
		self_id_count = find_and_insert_self_id(ohci, self_id_count);
		if (self_id_count < 0) {
			ohci_notice(ohci,
				    "could not construct local self ID\n");
			return;
		}
	}

	if (self_id_count == 0) {
		ohci_notice(ohci, "no self IDs\n");
		return;
	}
	rmb();

	/*
	 * Check the consistency of the self IDs we just read.  The
	 * problem we face is that a new bus reset can start while we
	 * read out the self IDs from the DMA buffer. If this happens,
	 * the DMA buffer will be overwritten with new self IDs and we
	 * will read out inconsistent data.  The OHCI specification
	 * (section 11.2) recommends a technique similar to
	 * linux/seqlock.h, where we remember the generation of the
	 * self IDs in the buffer before reading them out and compare
	 * it to the current generation after reading them out.  If
	 * the two generations match we know we have a consistent set
	 * of self IDs.
	 */

	new_generation = (reg_read(ohci, OHCI1394_SelfIDCount) >> 16) & 0xff;
	if (new_generation != generation) {
		ohci_notice(ohci, "new bus reset, discarding self ids\n");
		return;
	}

	/* FIXME: Document how the locking works. */
	spin_lock_irq(&ohci->lock);

	ohci->generation = -1; /* prevent AT packet queueing */
	context_stop(&ohci->at_request_ctx);
	context_stop(&ohci->at_response_ctx);

	spin_unlock_irq(&ohci->lock);

	/*
	 * Per OHCI 1.2 draft, clause 7.2.3.3, hardware may leave unsent
	 * packets in the AT queues and software needs to drain them.
	 * Some OHCI 1.1 controllers (JMicron) apparently require this too.
	 */
	at_context_flush(&ohci->at_request_ctx);
	at_context_flush(&ohci->at_response_ctx);

	spin_lock_irq(&ohci->lock);

	ohci->generation = generation;
	reg_write(ohci, OHCI1394_IntEventClear, OHCI1394_busReset);
	if (param_debug & OHCI_PARAM_DEBUG_BUSRESETS)
		reg_write(ohci, OHCI1394_IntMaskSet, OHCI1394_busReset);

	if (ohci->quirks & QUIRK_RESET_PACKET)
		ohci->request_generation = generation;

	/*
	 * This next bit is unrelated to the AT context stuff but we
	 * have to do it under the spinlock also.  If a new config rom
	 * was set up before this reset, the old one is now no longer
	 * in use and we can free it. Update the config rom pointers
	 * to point to the current config rom and clear the
	 * next_config_rom pointer so a new update can take place.
	 */

	if (ohci->next_config_rom != NULL) {
		if (ohci->next_config_rom != ohci->config_rom) {
			free_rom      = ohci->config_rom;
			free_rom_bus  = ohci->config_rom_bus;
		}
		ohci->config_rom      = ohci->next_config_rom;
		ohci->config_rom_bus  = ohci->next_config_rom_bus;
		ohci->next_config_rom = NULL;

		/*
		 * Restore config_rom image and manually update
		 * config_rom registers.  Writing the header quadlet
		 * will indicate that the config rom is ready, so we
		 * do that last.
		 */
		reg_write(ohci, OHCI1394_BusOptions,
			  be32_to_cpu(ohci->config_rom[2]));
		ohci->config_rom[0] = ohci->next_header;
		reg_write(ohci, OHCI1394_ConfigROMhdr,
			  be32_to_cpu(ohci->next_header));
	}

	if (param_remote_dma) {
		reg_write(ohci, OHCI1394_PhyReqFilterHiSet, ~0);
		reg_write(ohci, OHCI1394_PhyReqFilterLoSet, ~0);
	}

	spin_unlock_irq(&ohci->lock);

	if (free_rom)
		dmam_free_coherent(ohci->card.device, CONFIG_ROM_SIZE, free_rom, free_rom_bus);

	log_selfids(ohci, generation, self_id_count);

	fw_core_handle_bus_reset(&ohci->card, ohci->node_id, generation,
				 self_id_count, ohci->self_id_buffer,
				 ohci->csr_state_setclear_abdicate);
	ohci->csr_state_setclear_abdicate = false;
}

static irqreturn_t irq_handler(int irq, void *data)
{
	struct fw_ohci *ohci = data;
	u32 event, iso_event;
	int i;

	event = reg_read(ohci, OHCI1394_IntEventClear);

	if (!event || !~event)
		return IRQ_NONE;

	/*
	 * busReset and postedWriteErr events must not be cleared yet
	 * (OHCI 1.1 clauses 7.2.3.2 and 13.2.8.1)
	 */
	reg_write(ohci, OHCI1394_IntEventClear,
		  event & ~(OHCI1394_busReset | OHCI1394_postedWriteErr));
	log_irqs(ohci, event);
	if (event & OHCI1394_busReset)
		reg_write(ohci, OHCI1394_IntMaskClear, OHCI1394_busReset);

	if (event & OHCI1394_selfIDComplete)
		queue_work(selfid_workqueue, &ohci->bus_reset_work);

	if (event & OHCI1394_RQPkt)
		tasklet_schedule(&ohci->ar_request_ctx.tasklet);

	if (event & OHCI1394_RSPkt)
		tasklet_schedule(&ohci->ar_response_ctx.tasklet);

	if (event & OHCI1394_reqTxComplete)
		tasklet_schedule(&ohci->at_request_ctx.tasklet);

	if (event & OHCI1394_respTxComplete)
		tasklet_schedule(&ohci->at_response_ctx.tasklet);

	if (event & OHCI1394_isochRx) {
		iso_event = reg_read(ohci, OHCI1394_IsoRecvIntEventClear);
		reg_write(ohci, OHCI1394_IsoRecvIntEventClear, iso_event);

		while (iso_event) {
			i = ffs(iso_event) - 1;
			tasklet_schedule(
				&ohci->ir_context_list[i].context.tasklet);
			iso_event &= ~(1 << i);
		}
	}

	if (event & OHCI1394_isochTx) {
		iso_event = reg_read(ohci, OHCI1394_IsoXmitIntEventClear);
		reg_write(ohci, OHCI1394_IsoXmitIntEventClear, iso_event);

		while (iso_event) {
			i = ffs(iso_event) - 1;
			tasklet_schedule(
				&ohci->it_context_list[i].context.tasklet);
			iso_event &= ~(1 << i);
		}
	}

	if (unlikely(event & OHCI1394_regAccessFail))
		ohci_err(ohci, "register access failure\n");

	if (unlikely(event & OHCI1394_postedWriteErr)) {
		reg_read(ohci, OHCI1394_PostedWriteAddressHi);
		reg_read(ohci, OHCI1394_PostedWriteAddressLo);
		reg_write(ohci, OHCI1394_IntEventClear,
			  OHCI1394_postedWriteErr);
		if (printk_ratelimit())
			ohci_err(ohci, "PCI posted write error\n");
	}

	if (unlikely(event & OHCI1394_cycleTooLong)) {
		if (printk_ratelimit())
			ohci_notice(ohci, "isochronous cycle too long\n");
		reg_write(ohci, OHCI1394_LinkControlSet,
			  OHCI1394_LinkControl_cycleMaster);
	}

	if (unlikely(event & OHCI1394_cycleInconsistent)) {
		/*
		 * We need to clear this event bit in order to make
		 * cycleMatch isochronous I/O work.  In theory we should
		 * stop active cycleMatch iso contexts now and restart
		 * them at least two cycles later.  (FIXME?)
		 */
		if (printk_ratelimit())
			ohci_notice(ohci, "isochronous cycle inconsistent\n");
	}

	if (unlikely(event & OHCI1394_unrecoverableError))
		handle_dead_contexts(ohci);

	if (event & OHCI1394_cycle64Seconds) {
		spin_lock(&ohci->lock);
		update_bus_time(ohci);
		spin_unlock(&ohci->lock);
	} else
		flush_writes(ohci);

	return IRQ_HANDLED;
}

static int software_reset(struct fw_ohci *ohci)
{
	u32 val;
	int i;

	reg_write(ohci, OHCI1394_HCControlSet, OHCI1394_HCControl_softReset);
	for (i = 0; i < 500; i++) {
		val = reg_read(ohci, OHCI1394_HCControlSet);
		if (!~val)
			return -ENODEV; /* Card was ejected. */

		if (!(val & OHCI1394_HCControl_softReset))
			return 0;

		msleep(1);
	}

	return -EBUSY;
}

static void copy_config_rom(__be32 *dest, const __be32 *src, size_t length)
{
	size_t size = length * 4;

	memcpy(dest, src, size);
	if (size < CONFIG_ROM_SIZE)
		memset(&dest[length], 0, CONFIG_ROM_SIZE - size);
}

static int configure_1394a_enhancements(struct fw_ohci *ohci)
{
	bool enable_1394a;
	int ret, clear, set, offset;

	/* Check if the driver should configure link and PHY. */
	if (!(reg_read(ohci, OHCI1394_HCControlSet) &
	      OHCI1394_HCControl_programPhyEnable))
		return 0;

	/* Paranoia: check whether the PHY supports 1394a, too. */
	enable_1394a = false;
	ret = read_phy_reg(ohci, 2);
	if (ret < 0)
		return ret;
	if ((ret & PHY_EXTENDED_REGISTERS) == PHY_EXTENDED_REGISTERS) {
		ret = read_paged_phy_reg(ohci, 1, 8);
		if (ret < 0)
			return ret;
		if (ret >= 1)
			enable_1394a = true;
	}

	if (ohci->quirks & QUIRK_NO_1394A)
		enable_1394a = false;

	/* Configure PHY and link consistently. */
	if (enable_1394a) {
		clear = 0;
		set = PHY_ENABLE_ACCEL | PHY_ENABLE_MULTI;
	} else {
		clear = PHY_ENABLE_ACCEL | PHY_ENABLE_MULTI;
		set = 0;
	}
	ret = update_phy_reg(ohci, 5, clear, set);
	if (ret < 0)
		return ret;

	if (enable_1394a)
		offset = OHCI1394_HCControlSet;
	else
		offset = OHCI1394_HCControlClear;
	reg_write(ohci, offset, OHCI1394_HCControl_aPhyEnhanceEnable);

	/* Clean up: configuration has been taken care of. */
	reg_write(ohci, OHCI1394_HCControlClear,
		  OHCI1394_HCControl_programPhyEnable);

	return 0;
}

static int probe_tsb41ba3d(struct fw_ohci *ohci)
{
	/* TI vendor ID = 0x080028, TSB41BA3D product ID = 0x833005 (sic) */
	static const u8 id[] = { 0x08, 0x00, 0x28, 0x83, 0x30, 0x05, };
	int reg, i;

	reg = read_phy_reg(ohci, 2);
	if (reg < 0)
		return reg;
	if ((reg & PHY_EXTENDED_REGISTERS) != PHY_EXTENDED_REGISTERS)
		return 0;

	for (i = ARRAY_SIZE(id) - 1; i >= 0; i--) {
		reg = read_paged_phy_reg(ohci, 1, i + 10);
		if (reg < 0)
			return reg;
		if (reg != id[i])
			return 0;
	}
	return 1;
}

static int ohci_enable(struct fw_card *card,
		       const __be32 *config_rom, size_t length)
{
	struct fw_ohci *ohci = fw_ohci(card);
	u32 lps, version, irqs;
	int i, ret;

	ret = software_reset(ohci);
	if (ret < 0) {
		ohci_err(ohci, "failed to reset ohci card\n");
		return ret;
	}

	/*
	 * Now enable LPS, which we need in order to start accessing
	 * most of the registers.  In fact, on some cards (ALI M5251),
	 * accessing registers in the SClk domain without LPS enabled
	 * will lock up the machine.  Wait 50msec to make sure we have
	 * full link enabled.  However, with some cards (well, at least
	 * a JMicron PCIe card), we have to try again sometimes.
	 *
	 * TI TSB82AA2 + TSB81BA3(A) cards signal LPS enabled early but
	 * cannot actually use the phy at that time.  These need tens of
	 * millisecods pause between LPS write and first phy access too.
	 */

	reg_write(ohci, OHCI1394_HCControlSet,
		  OHCI1394_HCControl_LPS |
		  OHCI1394_HCControl_postedWriteEnable);
	flush_writes(ohci);

	for (lps = 0, i = 0; !lps && i < 3; i++) {
		msleep(50);
		lps = reg_read(ohci, OHCI1394_HCControlSet) &
		      OHCI1394_HCControl_LPS;
	}

	if (!lps) {
		ohci_err(ohci, "failed to set Link Power Status\n");
		return -EIO;
	}

	if (ohci->quirks & QUIRK_TI_SLLZ059) {
		ret = probe_tsb41ba3d(ohci);
		if (ret < 0)
			return ret;
		if (ret)
			ohci_notice(ohci, "local TSB41BA3D phy\n");
		else
			ohci->quirks &= ~QUIRK_TI_SLLZ059;
	}

	reg_write(ohci, OHCI1394_HCControlClear,
		  OHCI1394_HCControl_noByteSwapData);

	reg_write(ohci, OHCI1394_SelfIDBuffer, ohci->self_id_bus);
	reg_write(ohci, OHCI1394_LinkControlSet,
		  OHCI1394_LinkControl_cycleTimerEnable |
		  OHCI1394_LinkControl_cycleMaster);

	reg_write(ohci, OHCI1394_ATRetries,
		  OHCI1394_MAX_AT_REQ_RETRIES |
		  (OHCI1394_MAX_AT_RESP_RETRIES << 4) |
		  (OHCI1394_MAX_PHYS_RESP_RETRIES << 8) |
		  (200 << 16));

	ohci->bus_time_running = false;

	for (i = 0; i < 32; i++)
		if (ohci->ir_context_support & (1 << i))
			reg_write(ohci, OHCI1394_IsoRcvContextControlClear(i),
				  IR_CONTEXT_MULTI_CHANNEL_MODE);

	version = reg_read(ohci, OHCI1394_Version) & 0x00ff00ff;
	if (version >= OHCI_VERSION_1_1) {
		reg_write(ohci, OHCI1394_InitialChannelsAvailableHi,
			  0xfffffffe);
		card->broadcast_channel_auto_allocated = true;
	}

	/* Get implemented bits of the priority arbitration request counter. */
	reg_write(ohci, OHCI1394_FairnessControl, 0x3f);
	ohci->pri_req_max = reg_read(ohci, OHCI1394_FairnessControl) & 0x3f;
	reg_write(ohci, OHCI1394_FairnessControl, 0);
	card->priority_budget_implemented = ohci->pri_req_max != 0;

	reg_write(ohci, OHCI1394_PhyUpperBound, FW_MAX_PHYSICAL_RANGE >> 16);
	reg_write(ohci, OHCI1394_IntEventClear, ~0);
	reg_write(ohci, OHCI1394_IntMaskClear, ~0);

	ret = configure_1394a_enhancements(ohci);
	if (ret < 0)
		return ret;

	/* Activate link_on bit and contender bit in our self ID packets.*/
	ret = ohci_update_phy_reg(card, 4, 0, PHY_LINK_ACTIVE | PHY_CONTENDER);
	if (ret < 0)
		return ret;

	/*
	 * When the link is not yet enabled, the atomic config rom
	 * update mechanism described below in ohci_set_config_rom()
	 * is not active.  We have to update ConfigRomHeader and
	 * BusOptions manually, and the write to ConfigROMmap takes
	 * effect immediately.  We tie this to the enabling of the
	 * link, so we have a valid config rom before enabling - the
	 * OHCI requires that ConfigROMhdr and BusOptions have valid
	 * values before enabling.
	 *
	 * However, when the ConfigROMmap is written, some controllers
	 * always read back quadlets 0 and 2 from the config rom to
	 * the ConfigRomHeader and BusOptions registers on bus reset.
	 * They shouldn't do that in this initial case where the link
	 * isn't enabled.  This means we have to use the same
	 * workaround here, setting the bus header to 0 and then write
	 * the right values in the bus reset tasklet.
	 */

	if (config_rom) {
		ohci->next_config_rom = dmam_alloc_coherent(ohci->card.device, CONFIG_ROM_SIZE,
							    &ohci->next_config_rom_bus, GFP_KERNEL);
		if (ohci->next_config_rom == NULL)
			return -ENOMEM;

		copy_config_rom(ohci->next_config_rom, config_rom, length);
	} else {
		/*
		 * In the suspend case, config_rom is NULL, which
		 * means that we just reuse the old config rom.
		 */
		ohci->next_config_rom = ohci->config_rom;
		ohci->next_config_rom_bus = ohci->config_rom_bus;
	}

	ohci->next_header = ohci->next_config_rom[0];
	ohci->next_config_rom[0] = 0;
	reg_write(ohci, OHCI1394_ConfigROMhdr, 0);
	reg_write(ohci, OHCI1394_BusOptions,
		  be32_to_cpu(ohci->next_config_rom[2]));
	reg_write(ohci, OHCI1394_ConfigROMmap, ohci->next_config_rom_bus);

	reg_write(ohci, OHCI1394_AsReqFilterHiSet, 0x80000000);

	irqs =	OHCI1394_reqTxComplete | OHCI1394_respTxComplete |
		OHCI1394_RQPkt | OHCI1394_RSPkt |
		OHCI1394_isochTx | OHCI1394_isochRx |
		OHCI1394_postedWriteErr |
		OHCI1394_selfIDComplete |
		OHCI1394_regAccessFail |
		OHCI1394_cycleInconsistent |
		OHCI1394_unrecoverableError |
		OHCI1394_cycleTooLong |
		OHCI1394_masterIntEnable;
	if (param_debug & OHCI_PARAM_DEBUG_BUSRESETS)
		irqs |= OHCI1394_busReset;
	reg_write(ohci, OHCI1394_IntMaskSet, irqs);

	reg_write(ohci, OHCI1394_HCControlSet,
		  OHCI1394_HCControl_linkEnable |
		  OHCI1394_HCControl_BIBimageValid);

	reg_write(ohci, OHCI1394_LinkControlSet,
		  OHCI1394_LinkControl_rcvSelfID |
		  OHCI1394_LinkControl_rcvPhyPkt);

	ar_context_run(&ohci->ar_request_ctx);
	ar_context_run(&ohci->ar_response_ctx);

	flush_writes(ohci);

	/* We are ready to go, reset bus to finish initialization. */
	fw_schedule_bus_reset(&ohci->card, false, true);

	return 0;
}

static int ohci_set_config_rom(struct fw_card *card,
			       const __be32 *config_rom, size_t length)
{
	struct fw_ohci *ohci;
	__be32 *next_config_rom;
	dma_addr_t next_config_rom_bus;

	ohci = fw_ohci(card);

	/*
	 * When the OHCI controller is enabled, the config rom update
	 * mechanism is a bit tricky, but easy enough to use.  See
	 * section 5.5.6 in the OHCI specification.
	 *
	 * The OHCI controller caches the new config rom address in a
	 * shadow register (ConfigROMmapNext) and needs a bus reset
	 * for the changes to take place.  When the bus reset is
	 * detected, the controller loads the new values for the
	 * ConfigRomHeader and BusOptions registers from the specified
	 * config rom and loads ConfigROMmap from the ConfigROMmapNext
	 * shadow register. All automatically and atomically.
	 *
	 * Now, there's a twist to this story.  The automatic load of
	 * ConfigRomHeader and BusOptions doesn't honor the
	 * noByteSwapData bit, so with a be32 config rom, the
	 * controller will load be32 values in to these registers
	 * during the atomic update, even on litte endian
	 * architectures.  The workaround we use is to put a 0 in the
	 * header quadlet; 0 is endian agnostic and means that the
	 * config rom isn't ready yet.  In the bus reset tasklet we
	 * then set up the real values for the two registers.
	 *
	 * We use ohci->lock to avoid racing with the code that sets
	 * ohci->next_config_rom to NULL (see bus_reset_work).
	 */

	next_config_rom = dmam_alloc_coherent(ohci->card.device, CONFIG_ROM_SIZE,
					      &next_config_rom_bus, GFP_KERNEL);
	if (next_config_rom == NULL)
		return -ENOMEM;

	spin_lock_irq(&ohci->lock);

	/*
	 * If there is not an already pending config_rom update,
	 * push our new allocation into the ohci->next_config_rom
	 * and then mark the local variable as null so that we
	 * won't deallocate the new buffer.
	 *
	 * OTOH, if there is a pending config_rom update, just
	 * use that buffer with the new config_rom data, and
	 * let this routine free the unused DMA allocation.
	 */

	if (ohci->next_config_rom == NULL) {
		ohci->next_config_rom = next_config_rom;
		ohci->next_config_rom_bus = next_config_rom_bus;
		next_config_rom = NULL;
	}

	copy_config_rom(ohci->next_config_rom, config_rom, length);

	ohci->next_header = config_rom[0];
	ohci->next_config_rom[0] = 0;

	reg_write(ohci, OHCI1394_ConfigROMmap, ohci->next_config_rom_bus);

	spin_unlock_irq(&ohci->lock);

	/* If we didn't use the DMA allocation, delete it. */
	if (next_config_rom != NULL) {
		dmam_free_coherent(ohci->card.device, CONFIG_ROM_SIZE, next_config_rom,
				   next_config_rom_bus);
	}

	/*
	 * Now initiate a bus reset to have the changes take
	 * effect. We clean up the old config rom memory and DMA
	 * mappings in the bus reset tasklet, since the OHCI
	 * controller could need to access it before the bus reset
	 * takes effect.
	 */

	fw_schedule_bus_reset(&ohci->card, true, true);

	return 0;
}

static void ohci_send_request(struct fw_card *card, struct fw_packet *packet)
{
	struct fw_ohci *ohci = fw_ohci(card);

	at_context_transmit(&ohci->at_request_ctx, packet);
}

static void ohci_send_response(struct fw_card *card, struct fw_packet *packet)
{
	struct fw_ohci *ohci = fw_ohci(card);

	at_context_transmit(&ohci->at_response_ctx, packet);
}

static int ohci_cancel_packet(struct fw_card *card, struct fw_packet *packet)
{
	struct fw_ohci *ohci = fw_ohci(card);
	struct context *ctx = &ohci->at_request_ctx;
	struct driver_data *driver_data = packet->driver_data;
	int ret = -ENOENT;

	tasklet_disable_in_atomic(&ctx->tasklet);

	if (packet->ack != 0)
		goto out;

	if (packet->payload_mapped)
		dma_unmap_single(ohci->card.device, packet->payload_bus,
				 packet->payload_length, DMA_TO_DEVICE);

	log_ar_at_event(ohci, 'T', packet->speed, packet->header, 0x20);
	driver_data->packet = NULL;
	packet->ack = RCODE_CANCELLED;

	// Timestamping on behalf of the hardware.
	packet->timestamp = cycle_time_to_ohci_tstamp(get_cycle_time(ohci));

	packet->callback(packet, &ohci->card, packet->ack);
	ret = 0;
 out:
	tasklet_enable(&ctx->tasklet);

	return ret;
}

static int ohci_enable_phys_dma(struct fw_card *card,
				int node_id, int generation)
{
	struct fw_ohci *ohci = fw_ohci(card);
	unsigned long flags;
	int n, ret = 0;

	if (param_remote_dma)
		return 0;

	/*
	 * FIXME:  Make sure this bitmask is cleared when we clear the busReset
	 * interrupt bit.  Clear physReqResourceAllBuses on bus reset.
	 */

	spin_lock_irqsave(&ohci->lock, flags);

	if (ohci->generation != generation) {
		ret = -ESTALE;
		goto out;
	}

	/*
	 * Note, if the node ID contains a non-local bus ID, physical DMA is
	 * enabled for _all_ nodes on remote buses.
	 */

	n = (node_id & 0xffc0) == LOCAL_BUS ? node_id & 0x3f : 63;
	if (n < 32)
		reg_write(ohci, OHCI1394_PhyReqFilterLoSet, 1 << n);
	else
		reg_write(ohci, OHCI1394_PhyReqFilterHiSet, 1 << (n - 32));

	flush_writes(ohci);
 out:
	spin_unlock_irqrestore(&ohci->lock, flags);

	return ret;
}

static u32 ohci_read_csr(struct fw_card *card, int csr_offset)
{
	struct fw_ohci *ohci = fw_ohci(card);
	unsigned long flags;
	u32 value;

	switch (csr_offset) {
	case CSR_STATE_CLEAR:
	case CSR_STATE_SET:
		if (ohci->is_root &&
		    (reg_read(ohci, OHCI1394_LinkControlSet) &
		     OHCI1394_LinkControl_cycleMaster))
			value = CSR_STATE_BIT_CMSTR;
		else
			value = 0;
		if (ohci->csr_state_setclear_abdicate)
			value |= CSR_STATE_BIT_ABDICATE;

		return value;

	case CSR_NODE_IDS:
		return reg_read(ohci, OHCI1394_NodeID) << 16;

	case CSR_CYCLE_TIME:
		return get_cycle_time(ohci);

	case CSR_BUS_TIME:
		/*
		 * We might be called just after the cycle timer has wrapped
		 * around but just before the cycle64Seconds handler, so we
		 * better check here, too, if the bus time needs to be updated.
		 */
		spin_lock_irqsave(&ohci->lock, flags);
		value = update_bus_time(ohci);
		spin_unlock_irqrestore(&ohci->lock, flags);
		return value;

	case CSR_BUSY_TIMEOUT:
		value = reg_read(ohci, OHCI1394_ATRetries);
		return (value >> 4) & 0x0ffff00f;

	case CSR_PRIORITY_BUDGET:
		return (reg_read(ohci, OHCI1394_FairnessControl) & 0x3f) |
			(ohci->pri_req_max << 8);

	default:
		WARN_ON(1);
		return 0;
	}
}

static void ohci_write_csr(struct fw_card *card, int csr_offset, u32 value)
{
	struct fw_ohci *ohci = fw_ohci(card);
	unsigned long flags;

	switch (csr_offset) {
	case CSR_STATE_CLEAR:
		if ((value & CSR_STATE_BIT_CMSTR) && ohci->is_root) {
			reg_write(ohci, OHCI1394_LinkControlClear,
				  OHCI1394_LinkControl_cycleMaster);
			flush_writes(ohci);
		}
		if (value & CSR_STATE_BIT_ABDICATE)
			ohci->csr_state_setclear_abdicate = false;
		break;

	case CSR_STATE_SET:
		if ((value & CSR_STATE_BIT_CMSTR) && ohci->is_root) {
			reg_write(ohci, OHCI1394_LinkControlSet,
				  OHCI1394_LinkControl_cycleMaster);
			flush_writes(ohci);
		}
		if (value & CSR_STATE_BIT_ABDICATE)
			ohci->csr_state_setclear_abdicate = true;
		break;

	case CSR_NODE_IDS:
		reg_write(ohci, OHCI1394_NodeID, value >> 16);
		flush_writes(ohci);
		break;

	case CSR_CYCLE_TIME:
		reg_write(ohci, OHCI1394_IsochronousCycleTimer, value);
		reg_write(ohci, OHCI1394_IntEventSet,
			  OHCI1394_cycleInconsistent);
		flush_writes(ohci);
		break;

	case CSR_BUS_TIME:
		spin_lock_irqsave(&ohci->lock, flags);
		ohci->bus_time = (update_bus_time(ohci) & 0x40) |
		                 (value & ~0x7f);
		spin_unlock_irqrestore(&ohci->lock, flags);
		break;

	case CSR_BUSY_TIMEOUT:
		value = (value & 0xf) | ((value & 0xf) << 4) |
			((value & 0xf) << 8) | ((value & 0x0ffff000) << 4);
		reg_write(ohci, OHCI1394_ATRetries, value);
		flush_writes(ohci);
		break;

	case CSR_PRIORITY_BUDGET:
		reg_write(ohci, OHCI1394_FairnessControl, value & 0x3f);
		flush_writes(ohci);
		break;

	default:
		WARN_ON(1);
		break;
	}
}

static void flush_iso_completions(struct iso_context *ctx)
{
	ctx->base.callback.sc(&ctx->base, ctx->last_timestamp,
			      ctx->header_length, ctx->header,
			      ctx->base.callback_data);
	ctx->header_length = 0;
}

static void copy_iso_headers(struct iso_context *ctx, const u32 *dma_hdr)
{
	u32 *ctx_hdr;

	if (ctx->header_length + ctx->base.header_size > PAGE_SIZE) {
		if (ctx->base.drop_overflow_headers)
			return;
		flush_iso_completions(ctx);
	}

	ctx_hdr = ctx->header + ctx->header_length;
	ctx->last_timestamp = (u16)le32_to_cpu((__force __le32)dma_hdr[0]);

	/*
	 * The two iso header quadlets are byteswapped to little
	 * endian by the controller, but we want to present them
	 * as big endian for consistency with the bus endianness.
	 */
	if (ctx->base.header_size > 0)
		ctx_hdr[0] = swab32(dma_hdr[1]); /* iso packet header */
	if (ctx->base.header_size > 4)
		ctx_hdr[1] = swab32(dma_hdr[0]); /* timestamp */
	if (ctx->base.header_size > 8)
		memcpy(&ctx_hdr[2], &dma_hdr[2], ctx->base.header_size - 8);
	ctx->header_length += ctx->base.header_size;
}

static int handle_ir_packet_per_buffer(struct context *context,
				       struct descriptor *d,
				       struct descriptor *last)
{
	struct iso_context *ctx =
		container_of(context, struct iso_context, context);
	struct descriptor *pd;
	u32 buffer_dma;

	for (pd = d; pd <= last; pd++)
		if (pd->transfer_status)
			break;
	if (pd > last)
		/* Descriptor(s) not done yet, stop iteration */
		return 0;

	while (!(d->control & cpu_to_le16(DESCRIPTOR_BRANCH_ALWAYS))) {
		d++;
		buffer_dma = le32_to_cpu(d->data_address);
		dma_sync_single_range_for_cpu(context->ohci->card.device,
					      buffer_dma & PAGE_MASK,
					      buffer_dma & ~PAGE_MASK,
					      le16_to_cpu(d->req_count),
					      DMA_FROM_DEVICE);
	}

	copy_iso_headers(ctx, (u32 *) (last + 1));

	if (last->control & cpu_to_le16(DESCRIPTOR_IRQ_ALWAYS))
		flush_iso_completions(ctx);

	return 1;
}

/* d == last because each descriptor block is only a single descriptor. */
static int handle_ir_buffer_fill(struct context *context,
				 struct descriptor *d,
				 struct descriptor *last)
{
	struct iso_context *ctx =
		container_of(context, struct iso_context, context);
	unsigned int req_count, res_count, completed;
	u32 buffer_dma;

	req_count = le16_to_cpu(last->req_count);
	res_count = le16_to_cpu(READ_ONCE(last->res_count));
	completed = req_count - res_count;
	buffer_dma = le32_to_cpu(last->data_address);

	if (completed > 0) {
		ctx->mc_buffer_bus = buffer_dma;
		ctx->mc_completed = completed;
	}

	if (res_count != 0)
		/* Descriptor(s) not done yet, stop iteration */
		return 0;

	dma_sync_single_range_for_cpu(context->ohci->card.device,
				      buffer_dma & PAGE_MASK,
				      buffer_dma & ~PAGE_MASK,
				      completed, DMA_FROM_DEVICE);

	if (last->control & cpu_to_le16(DESCRIPTOR_IRQ_ALWAYS)) {
		ctx->base.callback.mc(&ctx->base,
				      buffer_dma + completed,
				      ctx->base.callback_data);
		ctx->mc_completed = 0;
	}

	return 1;
}

static void flush_ir_buffer_fill(struct iso_context *ctx)
{
	dma_sync_single_range_for_cpu(ctx->context.ohci->card.device,
				      ctx->mc_buffer_bus & PAGE_MASK,
				      ctx->mc_buffer_bus & ~PAGE_MASK,
				      ctx->mc_completed, DMA_FROM_DEVICE);

	ctx->base.callback.mc(&ctx->base,
			      ctx->mc_buffer_bus + ctx->mc_completed,
			      ctx->base.callback_data);
	ctx->mc_completed = 0;
}

static inline void sync_it_packet_for_cpu(struct context *context,
					  struct descriptor *pd)
{
	__le16 control;
	u32 buffer_dma;

	/* only packets beginning with OUTPUT_MORE* have data buffers */
	if (pd->control & cpu_to_le16(DESCRIPTOR_BRANCH_ALWAYS))
		return;

	/* skip over the OUTPUT_MORE_IMMEDIATE descriptor */
	pd += 2;

	/*
	 * If the packet has a header, the first OUTPUT_MORE/LAST descriptor's
	 * data buffer is in the context program's coherent page and must not
	 * be synced.
	 */
	if ((le32_to_cpu(pd->data_address) & PAGE_MASK) ==
	    (context->current_bus          & PAGE_MASK)) {
		if (pd->control & cpu_to_le16(DESCRIPTOR_BRANCH_ALWAYS))
			return;
		pd++;
	}

	do {
		buffer_dma = le32_to_cpu(pd->data_address);
		dma_sync_single_range_for_cpu(context->ohci->card.device,
					      buffer_dma & PAGE_MASK,
					      buffer_dma & ~PAGE_MASK,
					      le16_to_cpu(pd->req_count),
					      DMA_TO_DEVICE);
		control = pd->control;
		pd++;
	} while (!(control & cpu_to_le16(DESCRIPTOR_BRANCH_ALWAYS)));
}

static int handle_it_packet(struct context *context,
			    struct descriptor *d,
			    struct descriptor *last)
{
	struct iso_context *ctx =
		container_of(context, struct iso_context, context);
	struct descriptor *pd;
	__be32 *ctx_hdr;

	for (pd = d; pd <= last; pd++)
		if (pd->transfer_status)
			break;
	if (pd > last)
		/* Descriptor(s) not done yet, stop iteration */
		return 0;

	sync_it_packet_for_cpu(context, d);

	if (ctx->header_length + 4 > PAGE_SIZE) {
		if (ctx->base.drop_overflow_headers)
			return 1;
		flush_iso_completions(ctx);
	}

	ctx_hdr = ctx->header + ctx->header_length;
	ctx->last_timestamp = le16_to_cpu(last->res_count);
	/* Present this value as big-endian to match the receive code */
	*ctx_hdr = cpu_to_be32((le16_to_cpu(pd->transfer_status) << 16) |
			       le16_to_cpu(pd->res_count));
	ctx->header_length += 4;

	if (last->control & cpu_to_le16(DESCRIPTOR_IRQ_ALWAYS))
		flush_iso_completions(ctx);

	return 1;
}

static void set_multichannel_mask(struct fw_ohci *ohci, u64 channels)
{
	u32 hi = channels >> 32, lo = channels;

	reg_write(ohci, OHCI1394_IRMultiChanMaskHiClear, ~hi);
	reg_write(ohci, OHCI1394_IRMultiChanMaskLoClear, ~lo);
	reg_write(ohci, OHCI1394_IRMultiChanMaskHiSet, hi);
	reg_write(ohci, OHCI1394_IRMultiChanMaskLoSet, lo);
	ohci->mc_channels = channels;
}

static struct fw_iso_context *ohci_allocate_iso_context(struct fw_card *card,
				int type, int channel, size_t header_size)
{
	struct fw_ohci *ohci = fw_ohci(card);
	struct iso_context *ctx;
	descriptor_callback_t callback;
	u64 *channels;
	u32 *mask, regs;
	int index, ret = -EBUSY;

	spin_lock_irq(&ohci->lock);

	switch (type) {
	case FW_ISO_CONTEXT_TRANSMIT:
		mask     = &ohci->it_context_mask;
		callback = handle_it_packet;
		index    = ffs(*mask) - 1;
		if (index >= 0) {
			*mask &= ~(1 << index);
			regs = OHCI1394_IsoXmitContextBase(index);
			ctx  = &ohci->it_context_list[index];
		}
		break;

	case FW_ISO_CONTEXT_RECEIVE:
		channels = &ohci->ir_context_channels;
		mask     = &ohci->ir_context_mask;
		callback = handle_ir_packet_per_buffer;
		index    = *channels & 1ULL << channel ? ffs(*mask) - 1 : -1;
		if (index >= 0) {
			*channels &= ~(1ULL << channel);
			*mask     &= ~(1 << index);
			regs = OHCI1394_IsoRcvContextBase(index);
			ctx  = &ohci->ir_context_list[index];
		}
		break;

	case FW_ISO_CONTEXT_RECEIVE_MULTICHANNEL:
		mask     = &ohci->ir_context_mask;
		callback = handle_ir_buffer_fill;
		index    = !ohci->mc_allocated ? ffs(*mask) - 1 : -1;
		if (index >= 0) {
			ohci->mc_allocated = true;
			*mask &= ~(1 << index);
			regs = OHCI1394_IsoRcvContextBase(index);
			ctx  = &ohci->ir_context_list[index];
		}
		break;

	default:
		index = -1;
		ret = -ENOSYS;
	}

	spin_unlock_irq(&ohci->lock);

	if (index < 0)
		return ERR_PTR(ret);

	memset(ctx, 0, sizeof(*ctx));
	ctx->header_length = 0;
	ctx->header = (void *) __get_free_page(GFP_KERNEL);
	if (ctx->header == NULL) {
		ret = -ENOMEM;
		goto out;
	}
	ret = context_init(&ctx->context, ohci, regs, callback);
	if (ret < 0)
		goto out_with_header;

	if (type == FW_ISO_CONTEXT_RECEIVE_MULTICHANNEL) {
		set_multichannel_mask(ohci, 0);
		ctx->mc_completed = 0;
	}

	return &ctx->base;

 out_with_header:
	free_page((unsigned long)ctx->header);
 out:
	spin_lock_irq(&ohci->lock);

	switch (type) {
	case FW_ISO_CONTEXT_RECEIVE:
		*channels |= 1ULL << channel;
		break;

	case FW_ISO_CONTEXT_RECEIVE_MULTICHANNEL:
		ohci->mc_allocated = false;
		break;
	}
	*mask |= 1 << index;

	spin_unlock_irq(&ohci->lock);

	return ERR_PTR(ret);
}

static int ohci_start_iso(struct fw_iso_context *base,
			  s32 cycle, u32 sync, u32 tags)
{
	struct iso_context *ctx = container_of(base, struct iso_context, base);
	struct fw_ohci *ohci = ctx->context.ohci;
	u32 control = IR_CONTEXT_ISOCH_HEADER, match;
	int index;

	/* the controller cannot start without any queued packets */
	if (ctx->context.last->branch_address == 0)
		return -ENODATA;

	switch (ctx->base.type) {
	case FW_ISO_CONTEXT_TRANSMIT:
		index = ctx - ohci->it_context_list;
		match = 0;
		if (cycle >= 0)
			match = IT_CONTEXT_CYCLE_MATCH_ENABLE |
				(cycle & 0x7fff) << 16;

		reg_write(ohci, OHCI1394_IsoXmitIntEventClear, 1 << index);
		reg_write(ohci, OHCI1394_IsoXmitIntMaskSet, 1 << index);
		context_run(&ctx->context, match);
		break;

	case FW_ISO_CONTEXT_RECEIVE_MULTICHANNEL:
		control |= IR_CONTEXT_BUFFER_FILL|IR_CONTEXT_MULTI_CHANNEL_MODE;
		fallthrough;
	case FW_ISO_CONTEXT_RECEIVE:
		index = ctx - ohci->ir_context_list;
		match = (tags << 28) | (sync << 8) | ctx->base.channel;
		if (cycle >= 0) {
			match |= (cycle & 0x07fff) << 12;
			control |= IR_CONTEXT_CYCLE_MATCH_ENABLE;
		}

		reg_write(ohci, OHCI1394_IsoRecvIntEventClear, 1 << index);
		reg_write(ohci, OHCI1394_IsoRecvIntMaskSet, 1 << index);
		reg_write(ohci, CONTEXT_MATCH(ctx->context.regs), match);
		context_run(&ctx->context, control);

		ctx->sync = sync;
		ctx->tags = tags;

		break;
	}

	return 0;
}

static int ohci_stop_iso(struct fw_iso_context *base)
{
	struct fw_ohci *ohci = fw_ohci(base->card);
	struct iso_context *ctx = container_of(base, struct iso_context, base);
	int index;

	switch (ctx->base.type) {
	case FW_ISO_CONTEXT_TRANSMIT:
		index = ctx - ohci->it_context_list;
		reg_write(ohci, OHCI1394_IsoXmitIntMaskClear, 1 << index);
		break;

	case FW_ISO_CONTEXT_RECEIVE:
	case FW_ISO_CONTEXT_RECEIVE_MULTICHANNEL:
		index = ctx - ohci->ir_context_list;
		reg_write(ohci, OHCI1394_IsoRecvIntMaskClear, 1 << index);
		break;
	}
	flush_writes(ohci);
	context_stop(&ctx->context);
	tasklet_kill(&ctx->context.tasklet);

	return 0;
}

static void ohci_free_iso_context(struct fw_iso_context *base)
{
	struct fw_ohci *ohci = fw_ohci(base->card);
	struct iso_context *ctx = container_of(base, struct iso_context, base);
	unsigned long flags;
	int index;

	ohci_stop_iso(base);
	context_release(&ctx->context);
	free_page((unsigned long)ctx->header);

	spin_lock_irqsave(&ohci->lock, flags);

	switch (base->type) {
	case FW_ISO_CONTEXT_TRANSMIT:
		index = ctx - ohci->it_context_list;
		ohci->it_context_mask |= 1 << index;
		break;

	case FW_ISO_CONTEXT_RECEIVE:
		index = ctx - ohci->ir_context_list;
		ohci->ir_context_mask |= 1 << index;
		ohci->ir_context_channels |= 1ULL << base->channel;
		break;

	case FW_ISO_CONTEXT_RECEIVE_MULTICHANNEL:
		index = ctx - ohci->ir_context_list;
		ohci->ir_context_mask |= 1 << index;
		ohci->ir_context_channels |= ohci->mc_channels;
		ohci->mc_channels = 0;
		ohci->mc_allocated = false;
		break;
	}

	spin_unlock_irqrestore(&ohci->lock, flags);
}

static int ohci_set_iso_channels(struct fw_iso_context *base, u64 *channels)
{
	struct fw_ohci *ohci = fw_ohci(base->card);
	unsigned long flags;
	int ret;

	switch (base->type) {
	case FW_ISO_CONTEXT_RECEIVE_MULTICHANNEL:

		spin_lock_irqsave(&ohci->lock, flags);

		/* Don't allow multichannel to grab other contexts' channels. */
		if (~ohci->ir_context_channels & ~ohci->mc_channels & *channels) {
			*channels = ohci->ir_context_channels;
			ret = -EBUSY;
		} else {
			set_multichannel_mask(ohci, *channels);
			ret = 0;
		}

		spin_unlock_irqrestore(&ohci->lock, flags);

		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

#ifdef CONFIG_PM
static void ohci_resume_iso_dma(struct fw_ohci *ohci)
{
	int i;
	struct iso_context *ctx;

	for (i = 0 ; i < ohci->n_ir ; i++) {
		ctx = &ohci->ir_context_list[i];
		if (ctx->context.running)
			ohci_start_iso(&ctx->base, 0, ctx->sync, ctx->tags);
	}

	for (i = 0 ; i < ohci->n_it ; i++) {
		ctx = &ohci->it_context_list[i];
		if (ctx->context.running)
			ohci_start_iso(&ctx->base, 0, ctx->sync, ctx->tags);
	}
}
#endif

static int queue_iso_transmit(struct iso_context *ctx,
			      struct fw_iso_packet *packet,
			      struct fw_iso_buffer *buffer,
			      unsigned long payload)
{
	struct descriptor *d, *last, *pd;
	struct fw_iso_packet *p;
	__le32 *header;
	dma_addr_t d_bus, page_bus;
	u32 z, header_z, payload_z, irq;
	u32 payload_index, payload_end_index, next_page_index;
	int page, end_page, i, length, offset;

	p = packet;
	payload_index = payload;

	if (p->skip)
		z = 1;
	else
		z = 2;
	if (p->header_length > 0)
		z++;

	/* Determine the first page the payload isn't contained in. */
	end_page = PAGE_ALIGN(payload_index + p->payload_length) >> PAGE_SHIFT;
	if (p->payload_length > 0)
		payload_z = end_page - (payload_index >> PAGE_SHIFT);
	else
		payload_z = 0;

	z += payload_z;

	/* Get header size in number of descriptors. */
	header_z = DIV_ROUND_UP(p->header_length, sizeof(*d));

	d = context_get_descriptors(&ctx->context, z + header_z, &d_bus);
	if (d == NULL)
		return -ENOMEM;

	if (!p->skip) {
		d[0].control   = cpu_to_le16(DESCRIPTOR_KEY_IMMEDIATE);
		d[0].req_count = cpu_to_le16(8);
		/*
		 * Link the skip address to this descriptor itself.  This causes
		 * a context to skip a cycle whenever lost cycles or FIFO
		 * overruns occur, without dropping the data.  The application
		 * should then decide whether this is an error condition or not.
		 * FIXME:  Make the context's cycle-lost behaviour configurable?
		 */
		d[0].branch_address = cpu_to_le32(d_bus | z);

		header = (__le32 *) &d[1];
		header[0] = cpu_to_le32(IT_HEADER_SY(p->sy) |
					IT_HEADER_TAG(p->tag) |
					IT_HEADER_TCODE(TCODE_STREAM_DATA) |
					IT_HEADER_CHANNEL(ctx->base.channel) |
					IT_HEADER_SPEED(ctx->base.speed));
		header[1] =
			cpu_to_le32(IT_HEADER_DATA_LENGTH(p->header_length +
							  p->payload_length));
	}

	if (p->header_length > 0) {
		d[2].req_count    = cpu_to_le16(p->header_length);
		d[2].data_address = cpu_to_le32(d_bus + z * sizeof(*d));
		memcpy(&d[z], p->header, p->header_length);
	}

	pd = d + z - payload_z;
	payload_end_index = payload_index + p->payload_length;
	for (i = 0; i < payload_z; i++) {
		page               = payload_index >> PAGE_SHIFT;
		offset             = payload_index & ~PAGE_MASK;
		next_page_index    = (page + 1) << PAGE_SHIFT;
		length             =
			min(next_page_index, payload_end_index) - payload_index;
		pd[i].req_count    = cpu_to_le16(length);

		page_bus = page_private(buffer->pages[page]);
		pd[i].data_address = cpu_to_le32(page_bus + offset);

		dma_sync_single_range_for_device(ctx->context.ohci->card.device,
						 page_bus, offset, length,
						 DMA_TO_DEVICE);

		payload_index += length;
	}

	if (p->interrupt)
		irq = DESCRIPTOR_IRQ_ALWAYS;
	else
		irq = DESCRIPTOR_NO_IRQ;

	last = z == 2 ? d : d + z - 1;
	last->control |= cpu_to_le16(DESCRIPTOR_OUTPUT_LAST |
				     DESCRIPTOR_STATUS |
				     DESCRIPTOR_BRANCH_ALWAYS |
				     irq);

	context_append(&ctx->context, d, z, header_z);

	return 0;
}

static int queue_iso_packet_per_buffer(struct iso_context *ctx,
				       struct fw_iso_packet *packet,
				       struct fw_iso_buffer *buffer,
				       unsigned long payload)
{
	struct device *device = ctx->context.ohci->card.device;
	struct descriptor *d, *pd;
	dma_addr_t d_bus, page_bus;
	u32 z, header_z, rest;
	int i, j, length;
	int page, offset, packet_count, header_size, payload_per_buffer;

	/*
	 * The OHCI controller puts the isochronous header and trailer in the
	 * buffer, so we need at least 8 bytes.
	 */
	packet_count = packet->header_length / ctx->base.header_size;
	header_size  = max(ctx->base.header_size, (size_t)8);

	/* Get header size in number of descriptors. */
	header_z = DIV_ROUND_UP(header_size, sizeof(*d));
	page     = payload >> PAGE_SHIFT;
	offset   = payload & ~PAGE_MASK;
	payload_per_buffer = packet->payload_length / packet_count;

	for (i = 0; i < packet_count; i++) {
		/* d points to the header descriptor */
		z = DIV_ROUND_UP(payload_per_buffer + offset, PAGE_SIZE) + 1;
		d = context_get_descriptors(&ctx->context,
				z + header_z, &d_bus);
		if (d == NULL)
			return -ENOMEM;

		d->control      = cpu_to_le16(DESCRIPTOR_STATUS |
					      DESCRIPTOR_INPUT_MORE);
		if (packet->skip && i == 0)
			d->control |= cpu_to_le16(DESCRIPTOR_WAIT);
		d->req_count    = cpu_to_le16(header_size);
		d->res_count    = d->req_count;
		d->transfer_status = 0;
		d->data_address = cpu_to_le32(d_bus + (z * sizeof(*d)));

		rest = payload_per_buffer;
		pd = d;
		for (j = 1; j < z; j++) {
			pd++;
			pd->control = cpu_to_le16(DESCRIPTOR_STATUS |
						  DESCRIPTOR_INPUT_MORE);

			if (offset + rest < PAGE_SIZE)
				length = rest;
			else
				length = PAGE_SIZE - offset;
			pd->req_count = cpu_to_le16(length);
			pd->res_count = pd->req_count;
			pd->transfer_status = 0;

			page_bus = page_private(buffer->pages[page]);
			pd->data_address = cpu_to_le32(page_bus + offset);

			dma_sync_single_range_for_device(device, page_bus,
							 offset, length,
							 DMA_FROM_DEVICE);

			offset = (offset + length) & ~PAGE_MASK;
			rest -= length;
			if (offset == 0)
				page++;
		}
		pd->control = cpu_to_le16(DESCRIPTOR_STATUS |
					  DESCRIPTOR_INPUT_LAST |
					  DESCRIPTOR_BRANCH_ALWAYS);
		if (packet->interrupt && i == packet_count - 1)
			pd->control |= cpu_to_le16(DESCRIPTOR_IRQ_ALWAYS);

		context_append(&ctx->context, d, z, header_z);
	}

	return 0;
}

static int queue_iso_buffer_fill(struct iso_context *ctx,
				 struct fw_iso_packet *packet,
				 struct fw_iso_buffer *buffer,
				 unsigned long payload)
{
	struct descriptor *d;
	dma_addr_t d_bus, page_bus;
	int page, offset, rest, z, i, length;

	page   = payload >> PAGE_SHIFT;
	offset = payload & ~PAGE_MASK;
	rest   = packet->payload_length;

	/* We need one descriptor for each page in the buffer. */
	z = DIV_ROUND_UP(offset + rest, PAGE_SIZE);

	if (WARN_ON(offset & 3 || rest & 3 || page + z > buffer->page_count))
		return -EFAULT;

	for (i = 0; i < z; i++) {
		d = context_get_descriptors(&ctx->context, 1, &d_bus);
		if (d == NULL)
			return -ENOMEM;

		d->control = cpu_to_le16(DESCRIPTOR_INPUT_MORE |
					 DESCRIPTOR_BRANCH_ALWAYS);
		if (packet->skip && i == 0)
			d->control |= cpu_to_le16(DESCRIPTOR_WAIT);
		if (packet->interrupt && i == z - 1)
			d->control |= cpu_to_le16(DESCRIPTOR_IRQ_ALWAYS);

		if (offset + rest < PAGE_SIZE)
			length = rest;
		else
			length = PAGE_SIZE - offset;
		d->req_count = cpu_to_le16(length);
		d->res_count = d->req_count;
		d->transfer_status = 0;

		page_bus = page_private(buffer->pages[page]);
		d->data_address = cpu_to_le32(page_bus + offset);

		dma_sync_single_range_for_device(ctx->context.ohci->card.device,
						 page_bus, offset, length,
						 DMA_FROM_DEVICE);

		rest -= length;
		offset = 0;
		page++;

		context_append(&ctx->context, d, 1, 0);
	}

	return 0;
}

static int ohci_queue_iso(struct fw_iso_context *base,
			  struct fw_iso_packet *packet,
			  struct fw_iso_buffer *buffer,
			  unsigned long payload)
{
	struct iso_context *ctx = container_of(base, struct iso_context, base);
	unsigned long flags;
	int ret = -ENOSYS;

	spin_lock_irqsave(&ctx->context.ohci->lock, flags);
	switch (base->type) {
	case FW_ISO_CONTEXT_TRANSMIT:
		ret = queue_iso_transmit(ctx, packet, buffer, payload);
		break;
	case FW_ISO_CONTEXT_RECEIVE:
		ret = queue_iso_packet_per_buffer(ctx, packet, buffer, payload);
		break;
	case FW_ISO_CONTEXT_RECEIVE_MULTICHANNEL:
		ret = queue_iso_buffer_fill(ctx, packet, buffer, payload);
		break;
	}
	spin_unlock_irqrestore(&ctx->context.ohci->lock, flags);

	return ret;
}

static void ohci_flush_queue_iso(struct fw_iso_context *base)
{
	struct context *ctx =
			&container_of(base, struct iso_context, base)->context;

	reg_write(ctx->ohci, CONTROL_SET(ctx->regs), CONTEXT_WAKE);
}

static int ohci_flush_iso_completions(struct fw_iso_context *base)
{
	struct iso_context *ctx = container_of(base, struct iso_context, base);
	int ret = 0;

	tasklet_disable_in_atomic(&ctx->context.tasklet);

	if (!test_and_set_bit_lock(0, &ctx->flushing_completions)) {
		context_tasklet((unsigned long)&ctx->context);

		switch (base->type) {
		case FW_ISO_CONTEXT_TRANSMIT:
		case FW_ISO_CONTEXT_RECEIVE:
			if (ctx->header_length != 0)
				flush_iso_completions(ctx);
			break;
		case FW_ISO_CONTEXT_RECEIVE_MULTICHANNEL:
			if (ctx->mc_completed != 0)
				flush_ir_buffer_fill(ctx);
			break;
		default:
			ret = -ENOSYS;
		}

		clear_bit_unlock(0, &ctx->flushing_completions);
		smp_mb__after_atomic();
	}

	tasklet_enable(&ctx->context.tasklet);

	return ret;
}

static const struct fw_card_driver ohci_driver = {
	.enable			= ohci_enable,
	.read_phy_reg		= ohci_read_phy_reg,
	.update_phy_reg		= ohci_update_phy_reg,
	.set_config_rom		= ohci_set_config_rom,
	.send_request		= ohci_send_request,
	.send_response		= ohci_send_response,
	.cancel_packet		= ohci_cancel_packet,
	.enable_phys_dma	= ohci_enable_phys_dma,
	.read_csr		= ohci_read_csr,
	.write_csr		= ohci_write_csr,

	.allocate_iso_context	= ohci_allocate_iso_context,
	.free_iso_context	= ohci_free_iso_context,
	.set_iso_channels	= ohci_set_iso_channels,
	.queue_iso		= ohci_queue_iso,
	.flush_queue_iso	= ohci_flush_queue_iso,
	.flush_iso_completions	= ohci_flush_iso_completions,
	.start_iso		= ohci_start_iso,
	.stop_iso		= ohci_stop_iso,
};

#ifdef CONFIG_PPC_PMAC
static void pmac_ohci_on(struct pci_dev *dev)
{
	if (machine_is(powermac)) {
		struct device_node *ofn = pci_device_to_OF_node(dev);

		if (ofn) {
			pmac_call_feature(PMAC_FTR_1394_CABLE_POWER, ofn, 0, 1);
			pmac_call_feature(PMAC_FTR_1394_ENABLE, ofn, 0, 1);
		}
	}
}

static void pmac_ohci_off(struct pci_dev *dev)
{
	if (machine_is(powermac)) {
		struct device_node *ofn = pci_device_to_OF_node(dev);

		if (ofn) {
			pmac_call_feature(PMAC_FTR_1394_ENABLE, ofn, 0, 0);
			pmac_call_feature(PMAC_FTR_1394_CABLE_POWER, ofn, 0, 0);
		}
	}
}
#else
static inline void pmac_ohci_on(struct pci_dev *dev) {}
static inline void pmac_ohci_off(struct pci_dev *dev) {}
#endif /* CONFIG_PPC_PMAC */

static void release_ohci(struct device *dev, void *data)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct fw_ohci *ohci = pci_get_drvdata(pdev);

	pmac_ohci_off(pdev);

	ar_context_release(&ohci->ar_response_ctx);
	ar_context_release(&ohci->ar_request_ctx);

	dev_notice(dev, "removed fw-ohci device\n");
}

static int pci_probe(struct pci_dev *dev,
			       const struct pci_device_id *ent)
{
	struct fw_ohci *ohci;
	u32 bus_options, max_receive, link_speed, version;
	u64 guid;
	int i, err;
	size_t size;

	if (dev->vendor == PCI_VENDOR_ID_PINNACLE_SYSTEMS) {
		dev_err(&dev->dev, "Pinnacle MovieBoard is not yet supported\n");
		return -ENOSYS;
	}

	ohci = devres_alloc(release_ohci, sizeof(*ohci), GFP_KERNEL);
	if (ohci == NULL)
		return -ENOMEM;
	fw_card_initialize(&ohci->card, &ohci_driver, &dev->dev);
	pci_set_drvdata(dev, ohci);
	pmac_ohci_on(dev);
	devres_add(&dev->dev, ohci);

	err = pcim_enable_device(dev);
	if (err) {
		dev_err(&dev->dev, "failed to enable OHCI hardware\n");
		return err;
	}

	pci_set_master(dev);
	pci_write_config_dword(dev, OHCI1394_PCI_HCI_Control, 0);

	spin_lock_init(&ohci->lock);
	mutex_init(&ohci->phy_reg_mutex);

	INIT_WORK(&ohci->bus_reset_work, bus_reset_work);

	if (!(pci_resource_flags(dev, 0) & IORESOURCE_MEM) ||
	    pci_resource_len(dev, 0) < OHCI1394_REGISTER_SIZE) {
		ohci_err(ohci, "invalid MMIO resource\n");
		return -ENXIO;
	}

	err = pcim_iomap_regions(dev, 1 << 0, ohci_driver_name);
	if (err) {
		ohci_err(ohci, "request and map MMIO resource unavailable\n");
		return -ENXIO;
	}
	ohci->registers = pcim_iomap_table(dev)[0];

	for (i = 0; i < ARRAY_SIZE(ohci_quirks); i++)
		if ((ohci_quirks[i].vendor == dev->vendor) &&
		    (ohci_quirks[i].device == (unsigned short)PCI_ANY_ID ||
		     ohci_quirks[i].device == dev->device) &&
		    (ohci_quirks[i].revision == (unsigned short)PCI_ANY_ID ||
		     ohci_quirks[i].revision >= dev->revision)) {
			ohci->quirks = ohci_quirks[i].flags;
			break;
		}
	if (param_quirks)
		ohci->quirks = param_quirks;

	if (detect_vt630x_with_asm1083_on_amd_ryzen_machine(dev))
		ohci->quirks |= QUIRK_REBOOT_BY_CYCLE_TIMER_READ;

	/*
	 * Because dma_alloc_coherent() allocates at least one page,
	 * we save space by using a common buffer for the AR request/
	 * response descriptors and the self IDs buffer.
	 */
	BUILD_BUG_ON(AR_BUFFERS * sizeof(struct descriptor) > PAGE_SIZE/4);
	BUILD_BUG_ON(SELF_ID_BUF_SIZE > PAGE_SIZE/2);
	ohci->misc_buffer = dmam_alloc_coherent(&dev->dev, PAGE_SIZE, &ohci->misc_buffer_bus,
						GFP_KERNEL);
	if (!ohci->misc_buffer)
		return -ENOMEM;

	err = ar_context_init(&ohci->ar_request_ctx, ohci, 0,
			      OHCI1394_AsReqRcvContextControlSet);
	if (err < 0)
		return err;

	err = ar_context_init(&ohci->ar_response_ctx, ohci, PAGE_SIZE/4,
			      OHCI1394_AsRspRcvContextControlSet);
	if (err < 0)
		return err;

	err = context_init(&ohci->at_request_ctx, ohci,
			   OHCI1394_AsReqTrContextControlSet, handle_at_packet);
	if (err < 0)
		return err;

	err = context_init(&ohci->at_response_ctx, ohci,
			   OHCI1394_AsRspTrContextControlSet, handle_at_packet);
	if (err < 0)
		return err;

	reg_write(ohci, OHCI1394_IsoRecvIntMaskSet, ~0);
	ohci->ir_context_channels = ~0ULL;
	ohci->ir_context_support = reg_read(ohci, OHCI1394_IsoRecvIntMaskSet);
	reg_write(ohci, OHCI1394_IsoRecvIntMaskClear, ~0);
	ohci->ir_context_mask = ohci->ir_context_support;
	ohci->n_ir = hweight32(ohci->ir_context_mask);
	size = sizeof(struct iso_context) * ohci->n_ir;
	ohci->ir_context_list = devm_kzalloc(&dev->dev, size, GFP_KERNEL);
	if (!ohci->ir_context_list)
		return -ENOMEM;

	reg_write(ohci, OHCI1394_IsoXmitIntMaskSet, ~0);
	ohci->it_context_support = reg_read(ohci, OHCI1394_IsoXmitIntMaskSet);
	/* JMicron JMB38x often shows 0 at first read, just ignore it */
	if (!ohci->it_context_support) {
		ohci_notice(ohci, "overriding IsoXmitIntMask\n");
		ohci->it_context_support = 0xf;
	}
	reg_write(ohci, OHCI1394_IsoXmitIntMaskClear, ~0);
	ohci->it_context_mask = ohci->it_context_support;
	ohci->n_it = hweight32(ohci->it_context_mask);
	size = sizeof(struct iso_context) * ohci->n_it;
	ohci->it_context_list = devm_kzalloc(&dev->dev, size, GFP_KERNEL);
	if (!ohci->it_context_list)
		return -ENOMEM;

	ohci->self_id     = ohci->misc_buffer     + PAGE_SIZE/2;
	ohci->self_id_bus = ohci->misc_buffer_bus + PAGE_SIZE/2;

	bus_options = reg_read(ohci, OHCI1394_BusOptions);
	max_receive = (bus_options >> 12) & 0xf;
	link_speed = bus_options & 0x7;
	guid = ((u64) reg_read(ohci, OHCI1394_GUIDHi) << 32) |
		reg_read(ohci, OHCI1394_GUIDLo);

	if (!(ohci->quirks & QUIRK_NO_MSI))
		pci_enable_msi(dev);
	err = devm_request_irq(&dev->dev, dev->irq, irq_handler,
			       pci_dev_msi_enabled(dev) ? 0 : IRQF_SHARED, ohci_driver_name, ohci);
	if (err < 0) {
		ohci_err(ohci, "failed to allocate interrupt %d\n", dev->irq);
		goto fail_msi;
	}

	err = fw_card_add(&ohci->card, max_receive, link_speed, guid);
	if (err)
		goto fail_msi;

	version = reg_read(ohci, OHCI1394_Version) & 0x00ff00ff;
	ohci_notice(ohci,
		    "added OHCI v%x.%x device as card %d, "
		    "%d IR + %d IT contexts, quirks 0x%x%s\n",
		    version >> 16, version & 0xff, ohci->card.index,
		    ohci->n_ir, ohci->n_it, ohci->quirks,
		    reg_read(ohci, OHCI1394_PhyUpperBound) ?
			", physUB" : "");

	return 0;

 fail_msi:
	devm_free_irq(&dev->dev, dev->irq, ohci);
	pci_disable_msi(dev);

	return err;
}

static void pci_remove(struct pci_dev *dev)
{
	struct fw_ohci *ohci = pci_get_drvdata(dev);

	/*
	 * If the removal is happening from the suspend state, LPS won't be
	 * enabled and host registers (eg., IntMaskClear) won't be accessible.
	 */
	if (reg_read(ohci, OHCI1394_HCControlSet) & OHCI1394_HCControl_LPS) {
		reg_write(ohci, OHCI1394_IntMaskClear, ~0);
		flush_writes(ohci);
	}
	cancel_work_sync(&ohci->bus_reset_work);
	fw_core_remove_card(&ohci->card);

	/*
	 * FIXME: Fail all pending packets here, now that the upper
	 * layers can't queue any more.
	 */

	software_reset(ohci);

	devm_free_irq(&dev->dev, dev->irq, ohci);
	pci_disable_msi(dev);

	dev_notice(&dev->dev, "removing fw-ohci device\n");
}

#ifdef CONFIG_PM
static int pci_suspend(struct pci_dev *dev, pm_message_t state)
{
	struct fw_ohci *ohci = pci_get_drvdata(dev);
	int err;

	software_reset(ohci);
	err = pci_save_state(dev);
	if (err) {
		ohci_err(ohci, "pci_save_state failed\n");
		return err;
	}
	err = pci_set_power_state(dev, pci_choose_state(dev, state));
	if (err)
		ohci_err(ohci, "pci_set_power_state failed with %d\n", err);
	pmac_ohci_off(dev);

	return 0;
}

static int pci_resume(struct pci_dev *dev)
{
	struct fw_ohci *ohci = pci_get_drvdata(dev);
	int err;

	pmac_ohci_on(dev);
	pci_set_power_state(dev, PCI_D0);
	pci_restore_state(dev);
	err = pci_enable_device(dev);
	if (err) {
		ohci_err(ohci, "pci_enable_device failed\n");
		return err;
	}

	/* Some systems don't setup GUID register on resume from ram  */
	if (!reg_read(ohci, OHCI1394_GUIDLo) &&
					!reg_read(ohci, OHCI1394_GUIDHi)) {
		reg_write(ohci, OHCI1394_GUIDLo, (u32)ohci->card.guid);
		reg_write(ohci, OHCI1394_GUIDHi, (u32)(ohci->card.guid >> 32));
	}

	err = ohci_enable(&ohci->card, NULL, 0);
	if (err)
		return err;

	ohci_resume_iso_dma(ohci);

	return 0;
}
#endif

static const struct pci_device_id pci_table[] = {
	{ PCI_DEVICE_CLASS(PCI_CLASS_SERIAL_FIREWIRE_OHCI, ~0) },
	{ }
};

MODULE_DEVICE_TABLE(pci, pci_table);

static struct pci_driver fw_ohci_pci_driver = {
	.name		= ohci_driver_name,
	.id_table	= pci_table,
	.probe		= pci_probe,
	.remove		= pci_remove,
#ifdef CONFIG_PM
	.resume		= pci_resume,
	.suspend	= pci_suspend,
#endif
};

static int __init fw_ohci_init(void)
{
	selfid_workqueue = alloc_workqueue(KBUILD_MODNAME, WQ_MEM_RECLAIM, 0);
	if (!selfid_workqueue)
		return -ENOMEM;

	return pci_register_driver(&fw_ohci_pci_driver);
}

static void __exit fw_ohci_cleanup(void)
{
	pci_unregister_driver(&fw_ohci_pci_driver);
	destroy_workqueue(selfid_workqueue);
}

module_init(fw_ohci_init);
module_exit(fw_ohci_cleanup);

MODULE_AUTHOR("Kristian Hoegsberg <krh@bitplanet.net>");
MODULE_DESCRIPTION("Driver for PCI OHCI IEEE1394 controllers");
MODULE_LICENSE("GPL");

/* Provide a module alias so root-on-sbp2 initrds don't break. */
MODULE_ALIAS("ohci1394");
