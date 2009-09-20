/*
 * bioscalls.c - the lowlevel layer of the PnPBIOS driver
 */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/linkage.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/pnp.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/slab.h>
#include <linux/kmod.h>
#include <linux/completion.h>
#include <linux/spinlock.h>

#include <asm/page.h>
#include <asm/desc.h>
#include <asm/system.h>
#include <asm/byteorder.h>

#include "pnpbios.h"

static struct {
	u16 offset;
	u16 segment;
} pnp_bios_callpoint;

/*
 * These are some opcodes for a "static asmlinkage"
 * As this code is *not* executed inside the linux kernel segment, but in a
 * alias at offset 0, we need a far return that can not be compiled by
 * default (please, prove me wrong! this is *really* ugly!)
 * This is the only way to get the bios to return into the kernel code,
 * because the bios code runs in 16 bit protected mode and therefore can only
 * return to the caller if the call is within the first 64kB, and the linux
 * kernel begins at offset 3GB...
 */

asmlinkage void pnp_bios_callfunc(void);

__asm__(".text			\n"
	__ALIGN_STR "\n"
	"pnp_bios_callfunc:\n"
	"	pushl %edx	\n"
	"	pushl %ecx	\n"
	"	pushl %ebx	\n"
	"	pushl %eax	\n"
	"	lcallw *pnp_bios_callpoint\n"
	"	addl $16, %esp	\n"
	"	lret		\n"
	".previous		\n");

#define Q2_SET_SEL(cpu, selname, address, size) \
do { \
	struct desc_struct *gdt = get_cpu_gdt_table((cpu)); \
	set_desc_base(&gdt[(selname) >> 3], (u32)(address)); \
	set_desc_limit(&gdt[(selname) >> 3], (size) - 1); \
} while(0)

static struct desc_struct bad_bios_desc = GDT_ENTRY_INIT(0x4092,
			(unsigned long)__va(0x400UL), PAGE_SIZE - 0x400 - 1);

/*
 * At some point we want to use this stack frame pointer to unwind
 * after PnP BIOS oopses.
 */

u32 pnp_bios_fault_esp;
u32 pnp_bios_fault_eip;
u32 pnp_bios_is_utter_crap = 0;

static spinlock_t pnp_bios_lock;

/*
 * Support Functions
 */

static inline u16 call_pnp_bios(u16 func, u16 arg1, u16 arg2, u16 arg3,
				u16 arg4, u16 arg5, u16 arg6, u16 arg7,
				void *ts1_base, u32 ts1_size,
				void *ts2_base, u32 ts2_size)
{
	unsigned long flags;
	u16 status;
	struct desc_struct save_desc_40;
	int cpu;

	/*
	 * PnP BIOSes are generally not terribly re-entrant.
	 * Also, don't rely on them to save everything correctly.
	 */
	if (pnp_bios_is_utter_crap)
		return PNP_FUNCTION_NOT_SUPPORTED;

	cpu = get_cpu();
	save_desc_40 = get_cpu_gdt_table(cpu)[0x40 / 8];
	get_cpu_gdt_table(cpu)[0x40 / 8] = bad_bios_desc;

	/* On some boxes IRQ's during PnP BIOS calls are deadly.  */
	spin_lock_irqsave(&pnp_bios_lock, flags);

	/* The lock prevents us bouncing CPU here */
	if (ts1_size)
		Q2_SET_SEL(smp_processor_id(), PNP_TS1, ts1_base, ts1_size);
	if (ts2_size)
		Q2_SET_SEL(smp_processor_id(), PNP_TS2, ts2_base, ts2_size);

	__asm__ __volatile__("pushl %%ebp\n\t"
			     "pushl %%edi\n\t"
			     "pushl %%esi\n\t"
			     "pushl %%ds\n\t"
			     "pushl %%es\n\t"
			     "pushl %%fs\n\t"
			     "pushl %%gs\n\t"
			     "pushfl\n\t"
			     "movl %%esp, pnp_bios_fault_esp\n\t"
			     "movl $1f, pnp_bios_fault_eip\n\t"
			     "lcall %5,%6\n\t"
			     "1:popfl\n\t"
			     "popl %%gs\n\t"
			     "popl %%fs\n\t"
			     "popl %%es\n\t"
			     "popl %%ds\n\t"
			     "popl %%esi\n\t"
			     "popl %%edi\n\t"
			     "popl %%ebp\n\t":"=a"(status)
			     :"0"((func) | (((u32) arg1) << 16)),
			     "b"((arg2) | (((u32) arg3) << 16)),
			     "c"((arg4) | (((u32) arg5) << 16)),
			     "d"((arg6) | (((u32) arg7) << 16)),
			     "i"(PNP_CS32), "i"(0)
			     :"memory");
	spin_unlock_irqrestore(&pnp_bios_lock, flags);

	get_cpu_gdt_table(cpu)[0x40 / 8] = save_desc_40;
	put_cpu();

	/* If we get here and this is set then the PnP BIOS faulted on us. */
	if (pnp_bios_is_utter_crap) {
		printk(KERN_ERR
		       "PnPBIOS: Warning! Your PnP BIOS caused a fatal error. Attempting to continue\n");
		printk(KERN_ERR
		       "PnPBIOS: You may need to reboot with the \"pnpbios=off\" option to operate stably\n");
		printk(KERN_ERR
		       "PnPBIOS: Check with your vendor for an updated BIOS\n");
	}

	return status;
}

void pnpbios_print_status(const char *module, u16 status)
{
	switch (status) {
	case PNP_SUCCESS:
		printk(KERN_ERR "PnPBIOS: %s: function successful\n", module);
		break;
	case PNP_NOT_SET_STATICALLY:
		printk(KERN_ERR "PnPBIOS: %s: unable to set static resources\n",
		       module);
		break;
	case PNP_UNKNOWN_FUNCTION:
		printk(KERN_ERR "PnPBIOS: %s: invalid function number passed\n",
		       module);
		break;
	case PNP_FUNCTION_NOT_SUPPORTED:
		printk(KERN_ERR
		       "PnPBIOS: %s: function not supported on this system\n",
		       module);
		break;
	case PNP_INVALID_HANDLE:
		printk(KERN_ERR "PnPBIOS: %s: invalid handle\n", module);
		break;
	case PNP_BAD_PARAMETER:
		printk(KERN_ERR "PnPBIOS: %s: invalid parameters were passed\n",
		       module);
		break;
	case PNP_SET_FAILED:
		printk(KERN_ERR "PnPBIOS: %s: unable to set resources\n",
		       module);
		break;
	case PNP_EVENTS_NOT_PENDING:
		printk(KERN_ERR "PnPBIOS: %s: no events are pending\n", module);
		break;
	case PNP_SYSTEM_NOT_DOCKED:
		printk(KERN_ERR "PnPBIOS: %s: the system is not docked\n",
		       module);
		break;
	case PNP_NO_ISA_PNP_CARDS:
		printk(KERN_ERR
		       "PnPBIOS: %s: no isapnp cards are installed on this system\n",
		       module);
		break;
	case PNP_UNABLE_TO_DETERMINE_DOCK_CAPABILITIES:
		printk(KERN_ERR
		       "PnPBIOS: %s: cannot determine the capabilities of the docking station\n",
		       module);
		break;
	case PNP_CONFIG_CHANGE_FAILED_NO_BATTERY:
		printk(KERN_ERR
		       "PnPBIOS: %s: unable to undock, the system does not have a battery\n",
		       module);
		break;
	case PNP_CONFIG_CHANGE_FAILED_RESOURCE_CONFLICT:
		printk(KERN_ERR
		       "PnPBIOS: %s: could not dock due to resource conflicts\n",
		       module);
		break;
	case PNP_BUFFER_TOO_SMALL:
		printk(KERN_ERR "PnPBIOS: %s: the buffer passed is too small\n",
		       module);
		break;
	case PNP_USE_ESCD_SUPPORT:
		printk(KERN_ERR "PnPBIOS: %s: use ESCD instead\n", module);
		break;
	case PNP_MESSAGE_NOT_SUPPORTED:
		printk(KERN_ERR "PnPBIOS: %s: the message is unsupported\n",
		       module);
		break;
	case PNP_HARDWARE_ERROR:
		printk(KERN_ERR "PnPBIOS: %s: a hardware failure has occured\n",
		       module);
		break;
	default:
		printk(KERN_ERR "PnPBIOS: %s: unexpected status 0x%x\n", module,
		       status);
		break;
	}
}

/*
 * PnP BIOS Low Level Calls
 */

#define PNP_GET_NUM_SYS_DEV_NODES		0x00
#define PNP_GET_SYS_DEV_NODE			0x01
#define PNP_SET_SYS_DEV_NODE			0x02
#define PNP_GET_EVENT				0x03
#define PNP_SEND_MESSAGE			0x04
#define PNP_GET_DOCKING_STATION_INFORMATION	0x05
#define PNP_SET_STATIC_ALLOCED_RES_INFO		0x09
#define PNP_GET_STATIC_ALLOCED_RES_INFO		0x0a
#define PNP_GET_APM_ID_TABLE			0x0b
#define PNP_GET_PNP_ISA_CONFIG_STRUC		0x40
#define PNP_GET_ESCD_INFO			0x41
#define PNP_READ_ESCD				0x42
#define PNP_WRITE_ESCD				0x43

/*
 * Call PnP BIOS with function 0x00, "get number of system device nodes"
 */
static int __pnp_bios_dev_node_info(struct pnp_dev_node_info *data)
{
	u16 status;

	if (!pnp_bios_present())
		return PNP_FUNCTION_NOT_SUPPORTED;
	status = call_pnp_bios(PNP_GET_NUM_SYS_DEV_NODES, 0, PNP_TS1, 2,
			       PNP_TS1, PNP_DS, 0, 0, data,
			       sizeof(struct pnp_dev_node_info), NULL, 0);
	data->no_nodes &= 0xff;
	return status;
}

int pnp_bios_dev_node_info(struct pnp_dev_node_info *data)
{
	int status = __pnp_bios_dev_node_info(data);

	if (status)
		pnpbios_print_status("dev_node_info", status);
	return status;
}

/*
 * Note that some PnP BIOSes (e.g., on Sony Vaio laptops) die a horrible
 * death if they are asked to access the "current" configuration.
 * Therefore, if it's a matter of indifference, it's better to call
 * get_dev_node() and set_dev_node() with boot=1 rather than with boot=0.
 */

/* 
 * Call PnP BIOS with function 0x01, "get system device node"
 * Input: *nodenum = desired node,
 *        boot = whether to get nonvolatile boot (!=0)
 *               or volatile current (0) config
 * Output: *nodenum=next node or 0xff if no more nodes
 */
static int __pnp_bios_get_dev_node(u8 *nodenum, char boot,
				   struct pnp_bios_node *data)
{
	u16 status;
	u16 tmp_nodenum;

	if (!pnp_bios_present())
		return PNP_FUNCTION_NOT_SUPPORTED;
	if (!boot && pnpbios_dont_use_current_config)
		return PNP_FUNCTION_NOT_SUPPORTED;
	tmp_nodenum = *nodenum;
	status = call_pnp_bios(PNP_GET_SYS_DEV_NODE, 0, PNP_TS1, 0, PNP_TS2,
			       boot ? 2 : 1, PNP_DS, 0, &tmp_nodenum,
			       sizeof(tmp_nodenum), data, 65536);
	*nodenum = tmp_nodenum;
	return status;
}

int pnp_bios_get_dev_node(u8 *nodenum, char boot, struct pnp_bios_node *data)
{
	int status;

	status = __pnp_bios_get_dev_node(nodenum, boot, data);
	if (status)
		pnpbios_print_status("get_dev_node", status);
	return status;
}

/*
 * Call PnP BIOS with function 0x02, "set system device node"
 * Input: *nodenum = desired node, 
 *        boot = whether to set nonvolatile boot (!=0)
 *               or volatile current (0) config
 */
static int __pnp_bios_set_dev_node(u8 nodenum, char boot,
				   struct pnp_bios_node *data)
{
	u16 status;

	if (!pnp_bios_present())
		return PNP_FUNCTION_NOT_SUPPORTED;
	if (!boot && pnpbios_dont_use_current_config)
		return PNP_FUNCTION_NOT_SUPPORTED;
	status = call_pnp_bios(PNP_SET_SYS_DEV_NODE, nodenum, 0, PNP_TS1,
			       boot ? 2 : 1, PNP_DS, 0, 0, data, 65536, NULL,
			       0);
	return status;
}

int pnp_bios_set_dev_node(u8 nodenum, char boot, struct pnp_bios_node *data)
{
	int status;

	status = __pnp_bios_set_dev_node(nodenum, boot, data);
	if (status) {
		pnpbios_print_status("set_dev_node", status);
		return status;
	}
	if (!boot) {		/* Update devlist */
		status = pnp_bios_get_dev_node(&nodenum, boot, data);
		if (status)
			return status;
	}
	return status;
}

/*
 * Call PnP BIOS with function 0x05, "get docking station information"
 */
int pnp_bios_dock_station_info(struct pnp_docking_station_info *data)
{
	u16 status;

	if (!pnp_bios_present())
		return PNP_FUNCTION_NOT_SUPPORTED;
	status = call_pnp_bios(PNP_GET_DOCKING_STATION_INFORMATION, 0, PNP_TS1,
			       PNP_DS, 0, 0, 0, 0, data,
			       sizeof(struct pnp_docking_station_info), NULL,
			       0);
	return status;
}

/*
 * Call PnP BIOS with function 0x0a, "get statically allocated resource
 * information"
 */
static int __pnp_bios_get_stat_res(char *info)
{
	u16 status;

	if (!pnp_bios_present())
		return PNP_FUNCTION_NOT_SUPPORTED;
	status = call_pnp_bios(PNP_GET_STATIC_ALLOCED_RES_INFO, 0, PNP_TS1,
			       PNP_DS, 0, 0, 0, 0, info, 65536, NULL, 0);
	return status;
}

int pnp_bios_get_stat_res(char *info)
{
	int status;

	status = __pnp_bios_get_stat_res(info);
	if (status)
		pnpbios_print_status("get_stat_res", status);
	return status;
}

/*
 * Call PnP BIOS with function 0x40, "get isa pnp configuration structure"
 */
static int __pnp_bios_isapnp_config(struct pnp_isa_config_struc *data)
{
	u16 status;

	if (!pnp_bios_present())
		return PNP_FUNCTION_NOT_SUPPORTED;
	status = call_pnp_bios(PNP_GET_PNP_ISA_CONFIG_STRUC, 0, PNP_TS1, PNP_DS,
			       0, 0, 0, 0, data,
			       sizeof(struct pnp_isa_config_struc), NULL, 0);
	return status;
}

int pnp_bios_isapnp_config(struct pnp_isa_config_struc *data)
{
	int status;

	status = __pnp_bios_isapnp_config(data);
	if (status)
		pnpbios_print_status("isapnp_config", status);
	return status;
}

/*
 * Call PnP BIOS with function 0x41, "get ESCD info"
 */
static int __pnp_bios_escd_info(struct escd_info_struc *data)
{
	u16 status;

	if (!pnp_bios_present())
		return ESCD_FUNCTION_NOT_SUPPORTED;
	status = call_pnp_bios(PNP_GET_ESCD_INFO, 0, PNP_TS1, 2, PNP_TS1, 4,
			       PNP_TS1, PNP_DS, data,
			       sizeof(struct escd_info_struc), NULL, 0);
	return status;
}

int pnp_bios_escd_info(struct escd_info_struc *data)
{
	int status;

	status = __pnp_bios_escd_info(data);
	if (status)
		pnpbios_print_status("escd_info", status);
	return status;
}

/*
 * Call PnP BIOS function 0x42, "read ESCD"
 * nvram_base is determined by calling escd_info
 */
static int __pnp_bios_read_escd(char *data, u32 nvram_base)
{
	u16 status;

	if (!pnp_bios_present())
		return ESCD_FUNCTION_NOT_SUPPORTED;
	status = call_pnp_bios(PNP_READ_ESCD, 0, PNP_TS1, PNP_TS2, PNP_DS, 0, 0,
			       0, data, 65536, __va(nvram_base), 65536);
	return status;
}

int pnp_bios_read_escd(char *data, u32 nvram_base)
{
	int status;

	status = __pnp_bios_read_escd(data, nvram_base);
	if (status)
		pnpbios_print_status("read_escd", status);
	return status;
}

void pnpbios_calls_init(union pnp_bios_install_struct *header)
{
	int i;

	spin_lock_init(&pnp_bios_lock);
	pnp_bios_callpoint.offset = header->fields.pm16offset;
	pnp_bios_callpoint.segment = PNP_CS16;

	for_each_possible_cpu(i) {
		struct desc_struct *gdt = get_cpu_gdt_table(i);
		if (!gdt)
			continue;
		set_desc_base(&gdt[GDT_ENTRY_PNPBIOS_CS32],
			 (unsigned long)&pnp_bios_callfunc);
		set_desc_base(&gdt[GDT_ENTRY_PNPBIOS_CS16],
			 (unsigned long)__va(header->fields.pm16cseg));
		set_desc_base(&gdt[GDT_ENTRY_PNPBIOS_DS],
			 (unsigned long)__va(header->fields.pm16dseg));
	}
}
