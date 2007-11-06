/*
 * PAL & SAL emulation.
 *
 * Copyright (C) 1998-2001 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */

#ifdef CONFIG_PCI
# include <linux/pci.h>
#endif

#include <linux/efi.h>
#include <asm/io.h>
#include <asm/pal.h>
#include <asm/sal.h>

#include "ssc.h"

#define MB	(1024*1024UL)

#define SIMPLE_MEMMAP	1

#if SIMPLE_MEMMAP
# define NUM_MEM_DESCS	4
#else
# define NUM_MEM_DESCS	16
#endif

static char fw_mem[(  sizeof(struct ia64_boot_param)
		    + sizeof(efi_system_table_t)
		    + sizeof(efi_runtime_services_t)
		    + 1*sizeof(efi_config_table_t)
		    + sizeof(struct ia64_sal_systab)
		    + sizeof(struct ia64_sal_desc_entry_point)
		    + NUM_MEM_DESCS*(sizeof(efi_memory_desc_t))
		    + 1024)] __attribute__ ((aligned (8)));

#define SECS_PER_HOUR   (60 * 60)
#define SECS_PER_DAY    (SECS_PER_HOUR * 24)

/* Compute the `struct tm' representation of *T,
   offset OFFSET seconds east of UTC,
   and store year, yday, mon, mday, wday, hour, min, sec into *TP.
   Return nonzero if successful.  */
int
offtime (unsigned long t, efi_time_t *tp)
{
	const unsigned short int __mon_yday[2][13] =
	{
		/* Normal years.  */
		{ 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365 },
		/* Leap years.  */
		{ 0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366 }
	};
	long int days, rem, y;
	const unsigned short int *ip;

	days = t / SECS_PER_DAY;
	rem = t % SECS_PER_DAY;
	while (rem < 0) {
		rem += SECS_PER_DAY;
		--days;
	}
	while (rem >= SECS_PER_DAY) {
		rem -= SECS_PER_DAY;
		++days;
	}
	tp->hour = rem / SECS_PER_HOUR;
	rem %= SECS_PER_HOUR;
	tp->minute = rem / 60;
	tp->second = rem % 60;
	/* January 1, 1970 was a Thursday.  */
	y = 1970;

#	define DIV(a, b) ((a) / (b) - ((a) % (b) < 0))
#	define LEAPS_THRU_END_OF(y) (DIV (y, 4) - DIV (y, 100) + DIV (y, 400))
#	define __isleap(year) \
	  ((year) % 4 == 0 && ((year) % 100 != 0 || (year) % 400 == 0))

	while (days < 0 || days >= (__isleap (y) ? 366 : 365)) {
		/* Guess a corrected year, assuming 365 days per year.  */
		long int yg = y + days / 365 - (days % 365 < 0);

		/* Adjust DAYS and Y to match the guessed year.  */
		days -= ((yg - y) * 365 + LEAPS_THRU_END_OF (yg - 1)
			 - LEAPS_THRU_END_OF (y - 1));
		y = yg;
	}
	tp->year = y;
	ip = __mon_yday[__isleap(y)];
	for (y = 11; days < (long int) ip[y]; --y)
		continue;
	days -= ip[y];
	tp->month = y + 1;
	tp->day = days + 1;
	return 1;
}

extern void pal_emulator_static (void);

/* Macro to emulate SAL call using legacy IN and OUT calls to CF8, CFC etc.. */

#define BUILD_CMD(addr)		((0x80000000 | (addr)) & ~3)

#define REG_OFFSET(addr)	(0x00000000000000FF & (addr))
#define DEVICE_FUNCTION(addr)	(0x000000000000FF00 & (addr))
#define BUS_NUMBER(addr)	(0x0000000000FF0000 & (addr))

static efi_status_t
fw_efi_get_time (efi_time_t *tm, efi_time_cap_t *tc)
{
#if defined(CONFIG_IA64_HP_SIM) || defined(CONFIG_IA64_GENERIC)
	struct {
		int tv_sec;	/* must be 32bits to work */
		int tv_usec;
	} tv32bits;

	ssc((unsigned long) &tv32bits, 0, 0, 0, SSC_GET_TOD);

	memset(tm, 0, sizeof(*tm));
	offtime(tv32bits.tv_sec, tm);

	if (tc)
		memset(tc, 0, sizeof(*tc));
#else
#	error Not implemented yet...
#endif
	return EFI_SUCCESS;
}

static void
efi_reset_system (int reset_type, efi_status_t status, unsigned long data_size, efi_char16_t *data)
{
#if defined(CONFIG_IA64_HP_SIM) || defined(CONFIG_IA64_GENERIC)
	ssc(status, 0, 0, 0, SSC_EXIT);
#else
#	error Not implemented yet...
#endif
}

static efi_status_t
efi_unimplemented (void)
{
	return EFI_UNSUPPORTED;
}

static struct sal_ret_values
sal_emulator (long index, unsigned long in1, unsigned long in2,
	      unsigned long in3, unsigned long in4, unsigned long in5,
	      unsigned long in6, unsigned long in7)
{
	long r9  = 0;
	long r10 = 0;
	long r11 = 0;
	long status;

	/*
	 * Don't do a "switch" here since that gives us code that
	 * isn't self-relocatable.
	 */
	status = 0;
	if (index == SAL_FREQ_BASE) {
		switch (in1) {
		      case SAL_FREQ_BASE_PLATFORM:
			r9 = 200000000;
			break;

		      case SAL_FREQ_BASE_INTERVAL_TIMER:
			/*
			 * Is this supposed to be the cr.itc frequency
			 * or something platform specific?  The SAL
			 * doc ain't exactly clear on this...
			 */
			r9 = 700000000;
			break;

		      case SAL_FREQ_BASE_REALTIME_CLOCK:
			r9 = 1;
			break;

		      default:
			status = -1;
			break;
		}
	} else if (index == SAL_SET_VECTORS) {
		;
	} else if (index == SAL_GET_STATE_INFO) {
		;
	} else if (index == SAL_GET_STATE_INFO_SIZE) {
		;
	} else if (index == SAL_CLEAR_STATE_INFO) {
		;
	} else if (index == SAL_MC_RENDEZ) {
		;
	} else if (index == SAL_MC_SET_PARAMS) {
		;
	} else if (index == SAL_CACHE_FLUSH) {
		;
	} else if (index == SAL_CACHE_INIT) {
		;
#ifdef CONFIG_PCI
	} else if (index == SAL_PCI_CONFIG_READ) {
		/*
		 * in1 contains the PCI configuration address and in2
		 * the size of the read.  The value that is read is
		 * returned via the general register r9.
		 */
                outl(BUILD_CMD(in1), 0xCF8);
                if (in2 == 1)                           /* Reading byte  */
                        r9 = inb(0xCFC + ((REG_OFFSET(in1) & 3)));
                else if (in2 == 2)                      /* Reading word  */
                        r9 = inw(0xCFC + ((REG_OFFSET(in1) & 2)));
                else                                    /* Reading dword */
                        r9 = inl(0xCFC);
                status = PCIBIOS_SUCCESSFUL;
	} else if (index == SAL_PCI_CONFIG_WRITE) {
	      	/*
		 * in1 contains the PCI configuration address, in2 the
		 * size of the write, and in3 the actual value to be
		 * written out.
		 */
                outl(BUILD_CMD(in1), 0xCF8);
                if (in2 == 1)                           /* Writing byte  */
                        outb(in3, 0xCFC + ((REG_OFFSET(in1) & 3)));
                else if (in2 == 2)                      /* Writing word  */
                        outw(in3, 0xCFC + ((REG_OFFSET(in1) & 2)));
                else                                    /* Writing dword */
                        outl(in3, 0xCFC);
                status = PCIBIOS_SUCCESSFUL;
#endif /* CONFIG_PCI */
	} else if (index == SAL_UPDATE_PAL) {
		;
	} else {
		status = -1;
	}
	return ((struct sal_ret_values) {status, r9, r10, r11});
}

struct ia64_boot_param *
sys_fw_init (const char *args, int arglen)
{
	efi_system_table_t *efi_systab;
	efi_runtime_services_t *efi_runtime;
	efi_config_table_t *efi_tables;
	struct ia64_sal_systab *sal_systab;
	efi_memory_desc_t *efi_memmap, *md;
	unsigned long *pal_desc, *sal_desc;
	struct ia64_sal_desc_entry_point *sal_ed;
	struct ia64_boot_param *bp;
	unsigned char checksum = 0;
	char *cp, *cmd_line;
	int i = 0;
#	define MAKE_MD(typ, attr, start, end)		\
	do {						\
		md = efi_memmap + i++;			\
		md->type = typ;				\
		md->pad = 0;				\
		md->phys_addr = start;			\
		md->virt_addr = 0;			\
		md->num_pages = (end - start) >> 12;	\
		md->attribute = attr;			\
	} while (0)

	memset(fw_mem, 0, sizeof(fw_mem));

	pal_desc = (unsigned long *) &pal_emulator_static;
	sal_desc = (unsigned long *) &sal_emulator;

	cp = fw_mem;
	efi_systab  = (void *) cp; cp += sizeof(*efi_systab);
	efi_runtime = (void *) cp; cp += sizeof(*efi_runtime);
	efi_tables  = (void *) cp; cp += sizeof(*efi_tables);
	sal_systab  = (void *) cp; cp += sizeof(*sal_systab);
	sal_ed      = (void *) cp; cp += sizeof(*sal_ed);
	efi_memmap  = (void *) cp; cp += NUM_MEM_DESCS*sizeof(*efi_memmap);
	bp	    = (void *) cp; cp += sizeof(*bp);
	cmd_line    = (void *) cp;

	if (args) {
		if (arglen >= 1024)
			arglen = 1023;
		memcpy(cmd_line, args, arglen);
	} else {
		arglen = 0;
	}
	cmd_line[arglen] = '\0';

	memset(efi_systab, 0, sizeof(*efi_systab));
	efi_systab->hdr.signature = EFI_SYSTEM_TABLE_SIGNATURE;
	efi_systab->hdr.revision  = ((1 << 16) | 00);
	efi_systab->hdr.headersize = sizeof(efi_systab->hdr);
	efi_systab->fw_vendor = __pa("H\0e\0w\0l\0e\0t\0t\0-\0P\0a\0c\0k\0a\0r\0d\0\0");
	efi_systab->fw_revision = 1;
	efi_systab->runtime = (void *) __pa(efi_runtime);
	efi_systab->nr_tables = 1;
	efi_systab->tables = __pa(efi_tables);

	efi_runtime->hdr.signature = EFI_RUNTIME_SERVICES_SIGNATURE;
	efi_runtime->hdr.revision = EFI_RUNTIME_SERVICES_REVISION;
	efi_runtime->hdr.headersize = sizeof(efi_runtime->hdr);
	efi_runtime->get_time = __pa(&fw_efi_get_time);
	efi_runtime->set_time = __pa(&efi_unimplemented);
	efi_runtime->get_wakeup_time = __pa(&efi_unimplemented);
	efi_runtime->set_wakeup_time = __pa(&efi_unimplemented);
	efi_runtime->set_virtual_address_map = __pa(&efi_unimplemented);
	efi_runtime->get_variable = __pa(&efi_unimplemented);
	efi_runtime->get_next_variable = __pa(&efi_unimplemented);
	efi_runtime->set_variable = __pa(&efi_unimplemented);
	efi_runtime->get_next_high_mono_count = __pa(&efi_unimplemented);
	efi_runtime->reset_system = __pa(&efi_reset_system);

	efi_tables->guid = SAL_SYSTEM_TABLE_GUID;
	efi_tables->table = __pa(sal_systab);

	/* fill in the SAL system table: */
	memcpy(sal_systab->signature, "SST_", 4);
	sal_systab->size = sizeof(*sal_systab);
	sal_systab->sal_rev_minor = 1;
	sal_systab->sal_rev_major = 0;
	sal_systab->entry_count = 1;

#ifdef CONFIG_IA64_GENERIC
        strcpy(sal_systab->oem_id, "Generic");
        strcpy(sal_systab->product_id, "IA-64 system");
#endif

#ifdef CONFIG_IA64_HP_SIM
	strcpy(sal_systab->oem_id, "Hewlett-Packard");
	strcpy(sal_systab->product_id, "HP-simulator");
#endif

	/* fill in an entry point: */
	sal_ed->type = SAL_DESC_ENTRY_POINT;
	sal_ed->pal_proc = __pa(pal_desc[0]);
	sal_ed->sal_proc = __pa(sal_desc[0]);
	sal_ed->gp = __pa(sal_desc[1]);

	for (cp = (char *) sal_systab; cp < (char *) efi_memmap; ++cp)
		checksum += *cp;

	sal_systab->checksum = -checksum;

#if SIMPLE_MEMMAP
	/* simulate free memory at physical address zero */
	MAKE_MD(EFI_BOOT_SERVICES_DATA,		EFI_MEMORY_WB,    0*MB,    1*MB);
	MAKE_MD(EFI_PAL_CODE,			EFI_MEMORY_WB,    1*MB,    2*MB);
	MAKE_MD(EFI_CONVENTIONAL_MEMORY,	EFI_MEMORY_WB,    2*MB,  130*MB);
	MAKE_MD(EFI_CONVENTIONAL_MEMORY,	EFI_MEMORY_WB, 4096*MB, 4128*MB);
#else
	MAKE_MD( 4,		   0x9, 0x0000000000000000, 0x0000000000001000);
	MAKE_MD( 7,		   0x9, 0x0000000000001000, 0x000000000008a000);
	MAKE_MD( 4,		   0x9, 0x000000000008a000, 0x00000000000a0000);
	MAKE_MD( 5, 0x8000000000000009, 0x00000000000c0000, 0x0000000000100000);
	MAKE_MD( 7,		   0x9, 0x0000000000100000, 0x0000000004400000);
	MAKE_MD( 2,		   0x9, 0x0000000004400000, 0x0000000004be5000);
	MAKE_MD( 7,		   0x9, 0x0000000004be5000, 0x000000007f77e000);
	MAKE_MD( 6, 0x8000000000000009, 0x000000007f77e000, 0x000000007fb94000);
	MAKE_MD( 6, 0x8000000000000009, 0x000000007fb94000, 0x000000007fb95000);
	MAKE_MD( 6, 0x8000000000000009, 0x000000007fb95000, 0x000000007fc00000);
	MAKE_MD(13, 0x8000000000000009, 0x000000007fc00000, 0x000000007fc3a000);
	MAKE_MD( 7,		   0x9, 0x000000007fc3a000, 0x000000007fea0000);
	MAKE_MD( 5, 0x8000000000000009, 0x000000007fea0000, 0x000000007fea8000);
	MAKE_MD( 7,		   0x9, 0x000000007fea8000, 0x000000007feab000);
	MAKE_MD( 5, 0x8000000000000009, 0x000000007feab000, 0x000000007ffff000);
	MAKE_MD( 7,		   0x9, 0x00000000ff400000, 0x0000000104000000);
#endif

	bp->efi_systab = __pa(&fw_mem);
	bp->efi_memmap = __pa(efi_memmap);
	bp->efi_memmap_size = NUM_MEM_DESCS*sizeof(efi_memory_desc_t);
	bp->efi_memdesc_size = sizeof(efi_memory_desc_t);
	bp->efi_memdesc_version = 1;
	bp->command_line = __pa(cmd_line);
	bp->console_info.num_cols = 80;
	bp->console_info.num_rows = 25;
	bp->console_info.orig_x = 0;
	bp->console_info.orig_y = 24;
	bp->fpswa = 0;

	return bp;
}
