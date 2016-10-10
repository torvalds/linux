/*
 *    Copyright IBM Corp. 2007, 2009
 *    Author(s): Hongjie Yang <hongjie@us.ibm.com>,
 *		 Heiko Carstens <heiko.carstens@de.ibm.com>
 */

#define KMSG_COMPONENT "setup"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/compiler.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/lockdep.h>
#include <linux/extable.h>
#include <linux/pfn.h>
#include <linux/uaccess.h>
#include <linux/kernel.h>
#include <asm/diag.h>
#include <asm/ebcdic.h>
#include <asm/ipl.h>
#include <asm/lowcore.h>
#include <asm/processor.h>
#include <asm/sections.h>
#include <asm/setup.h>
#include <asm/sysinfo.h>
#include <asm/cpcmd.h>
#include <asm/sclp.h>
#include <asm/facility.h>
#include "entry.h"

/*
 * Create a Kernel NSS if the SAVESYS= parameter is defined
 */
#define DEFSYS_CMD_SIZE		128
#define SAVESYS_CMD_SIZE	32

char kernel_nss_name[NSS_NAME_SIZE + 1];

static void __init setup_boot_command_line(void);

/*
 * Get the TOD clock running.
 */
static void __init reset_tod_clock(void)
{
	u64 time;

	if (store_tod_clock(&time) == 0)
		return;
	/* TOD clock not running. Set the clock to Unix Epoch. */
	if (set_tod_clock(TOD_UNIX_EPOCH) != 0 || store_tod_clock(&time) != 0)
		disabled_wait(0);

	sched_clock_base_cc = TOD_UNIX_EPOCH;
	S390_lowcore.last_update_clock = sched_clock_base_cc;
}

#ifdef CONFIG_SHARED_KERNEL
int __init savesys_ipl_nss(char *cmd, const int cmdlen);

asm(
	"	.section .init.text,\"ax\",@progbits\n"
	"	.align	4\n"
	"	.type	savesys_ipl_nss, @function\n"
	"savesys_ipl_nss:\n"
	"	stmg	6,15,48(15)\n"
	"	lgr	14,3\n"
	"	sam31\n"
	"	diag	2,14,0x8\n"
	"	sam64\n"
	"	lgr	2,14\n"
	"	lmg	6,15,48(15)\n"
	"	br	14\n"
	"	.size	savesys_ipl_nss, .-savesys_ipl_nss\n"
	"	.previous\n");

static __initdata char upper_command_line[COMMAND_LINE_SIZE];

static noinline __init void create_kernel_nss(void)
{
	unsigned int i, stext_pfn, eshared_pfn, end_pfn, min_size;
#ifdef CONFIG_BLK_DEV_INITRD
	unsigned int sinitrd_pfn, einitrd_pfn;
#endif
	int response;
	int hlen;
	size_t len;
	char *savesys_ptr;
	char defsys_cmd[DEFSYS_CMD_SIZE];
	char savesys_cmd[SAVESYS_CMD_SIZE];

	/* Do nothing if we are not running under VM */
	if (!MACHINE_IS_VM)
		return;

	/* Convert COMMAND_LINE to upper case */
	for (i = 0; i < strlen(boot_command_line); i++)
		upper_command_line[i] = toupper(boot_command_line[i]);

	savesys_ptr = strstr(upper_command_line, "SAVESYS=");

	if (!savesys_ptr)
		return;

	savesys_ptr += 8;    /* Point to the beginning of the NSS name */
	for (i = 0; i < NSS_NAME_SIZE; i++) {
		if (savesys_ptr[i] == ' ' || savesys_ptr[i] == '\0')
			break;
		kernel_nss_name[i] = savesys_ptr[i];
	}

	stext_pfn = PFN_DOWN(__pa(&_stext));
	eshared_pfn = PFN_DOWN(__pa(&_eshared));
	end_pfn = PFN_UP(__pa(&_end));
	min_size = end_pfn << 2;

	hlen = snprintf(defsys_cmd, DEFSYS_CMD_SIZE,
			"DEFSYS %s 00000-%.5X EW %.5X-%.5X SR %.5X-%.5X",
			kernel_nss_name, stext_pfn - 1, stext_pfn,
			eshared_pfn - 1, eshared_pfn, end_pfn);

#ifdef CONFIG_BLK_DEV_INITRD
	if (INITRD_START && INITRD_SIZE) {
		sinitrd_pfn = PFN_DOWN(__pa(INITRD_START));
		einitrd_pfn = PFN_UP(__pa(INITRD_START + INITRD_SIZE));
		min_size = einitrd_pfn << 2;
		hlen += snprintf(defsys_cmd + hlen, DEFSYS_CMD_SIZE - hlen,
				 " EW %.5X-%.5X", sinitrd_pfn, einitrd_pfn);
	}
#endif

	snprintf(defsys_cmd + hlen, DEFSYS_CMD_SIZE - hlen,
		 " EW MINSIZE=%.7iK PARMREGS=0-13", min_size);
	defsys_cmd[DEFSYS_CMD_SIZE - 1] = '\0';
	snprintf(savesys_cmd, SAVESYS_CMD_SIZE, "SAVESYS %s \n IPL %s",
		 kernel_nss_name, kernel_nss_name);
	savesys_cmd[SAVESYS_CMD_SIZE - 1] = '\0';

	__cpcmd(defsys_cmd, NULL, 0, &response);

	if (response != 0) {
		pr_err("Defining the Linux kernel NSS failed with rc=%d\n",
			response);
		kernel_nss_name[0] = '\0';
		return;
	}

	len = strlen(savesys_cmd);
	ASCEBC(savesys_cmd, len);
	response = savesys_ipl_nss(savesys_cmd, len);

	/* On success: response is equal to the command size,
	 *	       max SAVESYS_CMD_SIZE
	 * On error: response contains the numeric portion of cp error message.
	 *	     for SAVESYS it will be >= 263
	 *	     for missing privilege class, it will be 1
	 */
	if (response > SAVESYS_CMD_SIZE || response == 1) {
		pr_err("Saving the Linux kernel NSS failed with rc=%d\n",
			response);
		kernel_nss_name[0] = '\0';
		return;
	}

	/* re-initialize cputime accounting. */
	sched_clock_base_cc = get_tod_clock();
	S390_lowcore.last_update_clock = sched_clock_base_cc;
	S390_lowcore.last_update_timer = 0x7fffffffffffffffULL;
	S390_lowcore.user_timer = 0;
	S390_lowcore.system_timer = 0;
	asm volatile("SPT 0(%0)" : : "a" (&S390_lowcore.last_update_timer));

	/* re-setup boot command line with new ipl vm parms */
	ipl_update_parameters();
	setup_boot_command_line();

	ipl_flags = IPL_NSS_VALID;
}

#else /* CONFIG_SHARED_KERNEL */

static inline void create_kernel_nss(void) { }

#endif /* CONFIG_SHARED_KERNEL */

/*
 * Clear bss memory
 */
static noinline __init void clear_bss_section(void)
{
	memset(__bss_start, 0, __bss_stop - __bss_start);
}

/*
 * Initialize storage key for kernel pages
 */
static noinline __init void init_kernel_storage_key(void)
{
#if PAGE_DEFAULT_KEY
	unsigned long end_pfn, init_pfn;

	end_pfn = PFN_UP(__pa(&_end));

	for (init_pfn = 0 ; init_pfn < end_pfn; init_pfn++)
		page_set_storage_key(init_pfn << PAGE_SHIFT,
				     PAGE_DEFAULT_KEY, 0);
#endif
}

static __initdata char sysinfo_page[PAGE_SIZE] __aligned(PAGE_SIZE);

static noinline __init void detect_machine_type(void)
{
	struct sysinfo_3_2_2 *vmms = (struct sysinfo_3_2_2 *)&sysinfo_page;

	/* Check current-configuration-level */
	if (stsi(NULL, 0, 0, 0) <= 2) {
		S390_lowcore.machine_flags |= MACHINE_FLAG_LPAR;
		return;
	}
	/* Get virtual-machine cpu information. */
	if (stsi(vmms, 3, 2, 2) || !vmms->count)
		return;

	/* Running under KVM? If not we assume z/VM */
	if (!memcmp(vmms->vm[0].cpi, "\xd2\xe5\xd4", 3))
		S390_lowcore.machine_flags |= MACHINE_FLAG_KVM;
	else
		S390_lowcore.machine_flags |= MACHINE_FLAG_VM;
}

static noinline __init void setup_arch_string(void)
{
	struct sysinfo_1_1_1 *mach = (struct sysinfo_1_1_1 *)&sysinfo_page;

	if (stsi(mach, 1, 1, 1))
		return;
	EBCASC(mach->manufacturer, sizeof(mach->manufacturer));
	EBCASC(mach->type, sizeof(mach->type));
	EBCASC(mach->model, sizeof(mach->model));
	EBCASC(mach->model_capacity, sizeof(mach->model_capacity));
	dump_stack_set_arch_desc("%-16.16s %-4.4s %-16.16s %-16.16s (%s)",
				 mach->manufacturer,
				 mach->type,
				 mach->model,
				 mach->model_capacity,
				 MACHINE_IS_LPAR ? "LPAR" :
				 MACHINE_IS_VM ? "z/VM" :
				 MACHINE_IS_KVM ? "KVM" : "unknown");
}

static __init void setup_topology(void)
{
	int max_mnest;

	if (!test_facility(11))
		return;
	S390_lowcore.machine_flags |= MACHINE_FLAG_TOPOLOGY;
	for (max_mnest = 6; max_mnest > 1; max_mnest--) {
		if (stsi(&sysinfo_page, 15, 1, max_mnest) == 0)
			break;
	}
	topology_max_mnest = max_mnest;
}

static void early_pgm_check_handler(void)
{
	const struct exception_table_entry *fixup;
	unsigned long cr0, cr0_new;
	unsigned long addr;

	addr = S390_lowcore.program_old_psw.addr;
	fixup = search_exception_tables(addr);
	if (!fixup)
		disabled_wait(0);
	/* Disable low address protection before storing into lowcore. */
	__ctl_store(cr0, 0, 0);
	cr0_new = cr0 & ~(1UL << 28);
	__ctl_load(cr0_new, 0, 0);
	S390_lowcore.program_old_psw.addr = extable_fixup(fixup);
	__ctl_load(cr0, 0, 0);
}

static noinline __init void setup_lowcore_early(void)
{
	psw_t psw;

	psw.mask = PSW_MASK_BASE | PSW_DEFAULT_KEY | PSW_MASK_EA | PSW_MASK_BA;
	psw.addr = (unsigned long) s390_base_ext_handler;
	S390_lowcore.external_new_psw = psw;
	psw.addr = (unsigned long) s390_base_pgm_handler;
	S390_lowcore.program_new_psw = psw;
	s390_base_pgm_handler_fn = early_pgm_check_handler;
}

static noinline __init void setup_facility_list(void)
{
	stfle(S390_lowcore.stfle_fac_list,
	      ARRAY_SIZE(S390_lowcore.stfle_fac_list));
}

static __init void detect_diag9c(void)
{
	unsigned int cpu_address;
	int rc;

	cpu_address = stap();
	diag_stat_inc(DIAG_STAT_X09C);
	asm volatile(
		"	diag	%2,0,0x9c\n"
		"0:	la	%0,0\n"
		"1:\n"
		EX_TABLE(0b,1b)
		: "=d" (rc) : "0" (-EOPNOTSUPP), "d" (cpu_address) : "cc");
	if (!rc)
		S390_lowcore.machine_flags |= MACHINE_FLAG_DIAG9C;
}

static __init void detect_diag44(void)
{
	int rc;

	diag_stat_inc(DIAG_STAT_X044);
	asm volatile(
		"	diag	0,0,0x44\n"
		"0:	la	%0,0\n"
		"1:\n"
		EX_TABLE(0b,1b)
		: "=d" (rc) : "0" (-EOPNOTSUPP) : "cc");
	if (!rc)
		S390_lowcore.machine_flags |= MACHINE_FLAG_DIAG44;
}

static __init void detect_machine_facilities(void)
{
	if (test_facility(8)) {
		S390_lowcore.machine_flags |= MACHINE_FLAG_EDAT1;
		__ctl_set_bit(0, 23);
	}
	if (test_facility(78))
		S390_lowcore.machine_flags |= MACHINE_FLAG_EDAT2;
	if (test_facility(3))
		S390_lowcore.machine_flags |= MACHINE_FLAG_IDTE;
	if (test_facility(40))
		S390_lowcore.machine_flags |= MACHINE_FLAG_LPP;
	if (test_facility(50) && test_facility(73))
		S390_lowcore.machine_flags |= MACHINE_FLAG_TE;
	if (test_facility(51))
		S390_lowcore.machine_flags |= MACHINE_FLAG_TLB_LC;
	if (test_facility(129)) {
		S390_lowcore.machine_flags |= MACHINE_FLAG_VX;
		__ctl_set_bit(0, 17);
	}
}

static inline void save_vector_registers(void)
{
#ifdef CONFIG_CRASH_DUMP
	if (test_facility(129))
		save_vx_regs(boot_cpu_vector_save_area);
#endif
}

static int __init disable_vector_extension(char *str)
{
	S390_lowcore.machine_flags &= ~MACHINE_FLAG_VX;
	__ctl_clear_bit(0, 17);
	return 1;
}
early_param("novx", disable_vector_extension);

static int __init cad_setup(char *str)
{
	int val;

	get_option(&str, &val);
	if (val && test_facility(128))
		S390_lowcore.machine_flags |= MACHINE_FLAG_CAD;
	return 0;
}
early_param("cad", cad_setup);

static int __init cad_init(void)
{
	if (MACHINE_HAS_CAD)
		/* Enable problem state CAD. */
		__ctl_set_bit(2, 3);
	return 0;
}
early_initcall(cad_init);

static __init void rescue_initrd(void)
{
#ifdef CONFIG_BLK_DEV_INITRD
	unsigned long min_initrd_addr = (unsigned long) _end + (4UL << 20);
	/*
	 * Just like in case of IPL from VM reader we make sure there is a
	 * gap of 4MB between end of kernel and start of initrd.
	 * That way we can also be sure that saving an NSS will succeed,
	 * which however only requires different segments.
	 */
	if (!INITRD_START || !INITRD_SIZE)
		return;
	if (INITRD_START >= min_initrd_addr)
		return;
	memmove((void *) min_initrd_addr, (void *) INITRD_START, INITRD_SIZE);
	INITRD_START = min_initrd_addr;
#endif
}

/* Set up boot command line */
static void __init append_to_cmdline(size_t (*ipl_data)(char *, size_t))
{
	char *parm, *delim;
	size_t rc, len;

	len = strlen(boot_command_line);

	delim = boot_command_line + len;	/* '\0' character position */
	parm  = boot_command_line + len + 1;	/* append right after '\0' */

	rc = ipl_data(parm, COMMAND_LINE_SIZE - len - 1);
	if (rc) {
		if (*parm == '=')
			memmove(boot_command_line, parm + 1, rc);
		else
			*delim = ' ';		/* replace '\0' with space */
	}
}

static inline int has_ebcdic_char(const char *str)
{
	int i;

	for (i = 0; str[i]; i++)
		if (str[i] & 0x80)
			return 1;
	return 0;
}

static void __init setup_boot_command_line(void)
{
	COMMAND_LINE[ARCH_COMMAND_LINE_SIZE - 1] = 0;
	/* convert arch command line to ascii if necessary */
	if (has_ebcdic_char(COMMAND_LINE))
		EBCASC(COMMAND_LINE, ARCH_COMMAND_LINE_SIZE);
	/* copy arch command line */
	strlcpy(boot_command_line, strstrip(COMMAND_LINE),
		ARCH_COMMAND_LINE_SIZE);

	/* append IPL PARM data to the boot command line */
	if (MACHINE_IS_VM)
		append_to_cmdline(append_ipl_vmparm);

	append_to_cmdline(append_ipl_scpdata);
}

/*
 * Save ipl parameters, clear bss memory, initialize storage keys
 * and create a kernel NSS at startup if the SAVESYS= parm is defined
 */
void __init startup_init(void)
{
	reset_tod_clock();
	ipl_save_parameters();
	rescue_initrd();
	clear_bss_section();
	time_early_init();
	init_kernel_storage_key();
	lockdep_off();
	setup_lowcore_early();
	setup_facility_list();
	detect_machine_type();
	setup_arch_string();
	ipl_update_parameters();
	setup_boot_command_line();
	create_kernel_nss();
	detect_diag9c();
	detect_diag44();
	detect_machine_facilities();
	save_vector_registers();
	setup_topology();
	sclp_early_detect();
	lockdep_on();
}
