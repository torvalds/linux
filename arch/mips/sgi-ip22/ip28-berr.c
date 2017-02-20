/*
 * ip28-berr.c: Bus error handling.
 *
 * Copyright (C) 2002, 2003 Ladislav Michl (ladis@linux-mips.org)
 * Copyright (C) 2005 Peter Fuerst (pf@net.alphadv.de) - IP28
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/seq_file.h>

#include <asm/addrspace.h>
#include <asm/traps.h>
#include <asm/branch.h>
#include <asm/irq_regs.h>
#include <asm/sgi/mc.h>
#include <asm/sgi/hpc3.h>
#include <asm/sgi/ioc.h>
#include <asm/sgi/ip22.h>
#include <asm/r4kcache.h>
#include <linux/uaccess.h>
#include <asm/bootinfo.h>

static unsigned int count_be_is_fixup;
static unsigned int count_be_handler;
static unsigned int count_be_interrupt;
static int debug_be_interrupt;

static unsigned int cpu_err_stat;	/* Status reg for CPU */
static unsigned int gio_err_stat;	/* Status reg for GIO */
static unsigned int cpu_err_addr;	/* Error address reg for CPU */
static unsigned int gio_err_addr;	/* Error address reg for GIO */
static unsigned int extio_stat;
static unsigned int hpc3_berr_stat;	/* Bus error interrupt status */

struct hpc3_stat {
	unsigned long addr;
	unsigned int ctrl;
	unsigned int cbp;
	unsigned int ndptr;
};

static struct {
	struct hpc3_stat pbdma[8];
	struct hpc3_stat scsi[2];
	struct hpc3_stat ethrx, ethtx;
} hpc3;

static struct {
	unsigned long err_addr;
	struct {
		u32 lo;
		u32 hi;
	} tags[1][2], tagd[4][2], tagi[4][2]; /* Way 0/1 */
} cache_tags;

static inline void save_cache_tags(unsigned busaddr)
{
	unsigned long addr = CAC_BASE | busaddr;
	int i;
	cache_tags.err_addr = addr;

	/*
	 * Starting with a bus-address, save secondary cache (indexed by
	 * PA[23..18:7..6]) tags first.
	 */
	addr &= ~1L;
#define tag cache_tags.tags[0]
	cache_op(Index_Load_Tag_S, addr);
	tag[0].lo = read_c0_taglo();	/* PA[35:18], VA[13:12] */
	tag[0].hi = read_c0_taghi();	/* PA[39:36] */
	cache_op(Index_Load_Tag_S, addr | 1L);
	tag[1].lo = read_c0_taglo();	/* PA[35:18], VA[13:12] */
	tag[1].hi = read_c0_taghi();	/* PA[39:36] */
#undef tag

	/*
	 * Save all primary data cache (indexed by VA[13:5]) tags which
	 * might fit to this bus-address, knowing that VA[11:0] == PA[11:0].
	 * Saving all tags and evaluating them later is easier and safer
	 * than relying on VA[13:12] from the secondary cache tags to pick
	 * matching primary tags here already.
	 */
	addr &= (0xffL << 56) | ((1 << 12) - 1);
#define tag cache_tags.tagd[i]
	for (i = 0; i < 4; ++i, addr += (1 << 12)) {
		cache_op(Index_Load_Tag_D, addr);
		tag[0].lo = read_c0_taglo();	/* PA[35:12] */
		tag[0].hi = read_c0_taghi();	/* PA[39:36] */
		cache_op(Index_Load_Tag_D, addr | 1L);
		tag[1].lo = read_c0_taglo();	/* PA[35:12] */
		tag[1].hi = read_c0_taghi();	/* PA[39:36] */
	}
#undef tag

	/*
	 * Save primary instruction cache (indexed by VA[13:6]) tags
	 * the same way.
	 */
	addr &= (0xffL << 56) | ((1 << 12) - 1);
#define tag cache_tags.tagi[i]
	for (i = 0; i < 4; ++i, addr += (1 << 12)) {
		cache_op(Index_Load_Tag_I, addr);
		tag[0].lo = read_c0_taglo();	/* PA[35:12] */
		tag[0].hi = read_c0_taghi();	/* PA[39:36] */
		cache_op(Index_Load_Tag_I, addr | 1L);
		tag[1].lo = read_c0_taglo();	/* PA[35:12] */
		tag[1].hi = read_c0_taghi();	/* PA[39:36] */
	}
#undef tag
}

#define GIO_ERRMASK	0xff00
#define CPU_ERRMASK	0x3f00

static void save_and_clear_buserr(void)
{
	int i;

	/* save status registers */
	cpu_err_addr = sgimc->cerr;
	cpu_err_stat = sgimc->cstat;
	gio_err_addr = sgimc->gerr;
	gio_err_stat = sgimc->gstat;
	extio_stat = sgioc->extio;
	hpc3_berr_stat = hpc3c0->bestat;

	hpc3.scsi[0].addr  = (unsigned long)&hpc3c0->scsi_chan0;
	hpc3.scsi[0].ctrl  = hpc3c0->scsi_chan0.ctrl; /* HPC3_SCTRL_ACTIVE ? */
	hpc3.scsi[0].cbp   = hpc3c0->scsi_chan0.cbptr;
	hpc3.scsi[0].ndptr = hpc3c0->scsi_chan0.ndptr;

	hpc3.scsi[1].addr  = (unsigned long)&hpc3c0->scsi_chan1;
	hpc3.scsi[1].ctrl  = hpc3c0->scsi_chan1.ctrl; /* HPC3_SCTRL_ACTIVE ? */
	hpc3.scsi[1].cbp   = hpc3c0->scsi_chan1.cbptr;
	hpc3.scsi[1].ndptr = hpc3c0->scsi_chan1.ndptr;

	hpc3.ethrx.addr	 = (unsigned long)&hpc3c0->ethregs.rx_cbptr;
	hpc3.ethrx.ctrl	 = hpc3c0->ethregs.rx_ctrl; /* HPC3_ERXCTRL_ACTIVE ? */
	hpc3.ethrx.cbp	 = hpc3c0->ethregs.rx_cbptr;
	hpc3.ethrx.ndptr = hpc3c0->ethregs.rx_ndptr;

	hpc3.ethtx.addr	 = (unsigned long)&hpc3c0->ethregs.tx_cbptr;
	hpc3.ethtx.ctrl	 = hpc3c0->ethregs.tx_ctrl; /* HPC3_ETXCTRL_ACTIVE ? */
	hpc3.ethtx.cbp	 = hpc3c0->ethregs.tx_cbptr;
	hpc3.ethtx.ndptr = hpc3c0->ethregs.tx_ndptr;

	for (i = 0; i < 8; ++i) {
		/* HPC3_PDMACTRL_ISACT ? */
		hpc3.pbdma[i].addr  = (unsigned long)&hpc3c0->pbdma[i];
		hpc3.pbdma[i].ctrl  = hpc3c0->pbdma[i].pbdma_ctrl;
		hpc3.pbdma[i].cbp   = hpc3c0->pbdma[i].pbdma_bptr;
		hpc3.pbdma[i].ndptr = hpc3c0->pbdma[i].pbdma_dptr;
	}
	i = 0;
	if (gio_err_stat & CPU_ERRMASK)
		i = gio_err_addr;
	if (cpu_err_stat & CPU_ERRMASK)
		i = cpu_err_addr;
	save_cache_tags(i);

	sgimc->cstat = sgimc->gstat = 0;
}

static void print_cache_tags(void)
{
	u32 scb, scw;
	int i;

	printk(KERN_ERR "Cache tags @ %08x:\n", (unsigned)cache_tags.err_addr);

	/* PA[31:12] shifted to PTag0 (PA[35:12]) format */
	scw = (cache_tags.err_addr >> 4) & 0x0fffff00;

	scb = cache_tags.err_addr & ((1 << 12) - 1) & ~((1 << 5) - 1);
	for (i = 0; i < 4; ++i) { /* for each possible VA[13:12] value */
		if ((cache_tags.tagd[i][0].lo & 0x0fffff00) != scw &&
		    (cache_tags.tagd[i][1].lo & 0x0fffff00) != scw)
		    continue;
		printk(KERN_ERR
		       "D: 0: %08x %08x, 1: %08x %08x  (VA[13:5]  %04x)\n",
			cache_tags.tagd[i][0].hi, cache_tags.tagd[i][0].lo,
			cache_tags.tagd[i][1].hi, cache_tags.tagd[i][1].lo,
			scb | (1 << 12)*i);
	}
	scb = cache_tags.err_addr & ((1 << 12) - 1) & ~((1 << 6) - 1);
	for (i = 0; i < 4; ++i) { /* for each possible VA[13:12] value */
		if ((cache_tags.tagi[i][0].lo & 0x0fffff00) != scw &&
		    (cache_tags.tagi[i][1].lo & 0x0fffff00) != scw)
		    continue;
		printk(KERN_ERR
		       "I: 0: %08x %08x, 1: %08x %08x  (VA[13:6]  %04x)\n",
			cache_tags.tagi[i][0].hi, cache_tags.tagi[i][0].lo,
			cache_tags.tagi[i][1].hi, cache_tags.tagi[i][1].lo,
			scb | (1 << 12)*i);
	}
	i = read_c0_config();
	scb = i & (1 << 13) ? 7:6;	/* scblksize = 2^[7..6] */
	scw = ((i >> 16) & 7) + 19 - 1; /* scwaysize = 2^[24..19] / 2 */

	i = ((1 << scw) - 1) & ~((1 << scb) - 1);
	printk(KERN_ERR "S: 0: %08x %08x, 1: %08x %08x	(PA[%u:%u] %05x)\n",
		cache_tags.tags[0][0].hi, cache_tags.tags[0][0].lo,
		cache_tags.tags[0][1].hi, cache_tags.tags[0][1].lo,
		scw-1, scb, i & (unsigned)cache_tags.err_addr);
}

static inline const char *cause_excode_text(int cause)
{
	static const char *txt[32] =
	{	"Interrupt",
		"TLB modification",
		"TLB (load or instruction fetch)",
		"TLB (store)",
		"Address error (load or instruction fetch)",
		"Address error (store)",
		"Bus error (instruction fetch)",
		"Bus error (data: load or store)",
		"Syscall",
		"Breakpoint",
		"Reserved instruction",
		"Coprocessor unusable",
		"Arithmetic Overflow",
		"Trap",
		"14",
		"Floating-Point",
		"16", "17", "18", "19", "20", "21", "22",
		"Watch Hi/Lo",
		"24", "25", "26", "27", "28", "29", "30", "31",
	};
	return txt[(cause & 0x7c) >> 2];
}

static void print_buserr(const struct pt_regs *regs)
{
	const int field = 2 * sizeof(unsigned long);
	int error = 0;

	if (extio_stat & EXTIO_MC_BUSERR) {
		printk(KERN_ERR "MC Bus Error\n");
		error |= 1;
	}
	if (extio_stat & EXTIO_HPC3_BUSERR) {
		printk(KERN_ERR "HPC3 Bus Error 0x%x:<id=0x%x,%s,lane=0x%x>\n",
			hpc3_berr_stat,
			(hpc3_berr_stat & HPC3_BESTAT_PIDMASK) >>
					  HPC3_BESTAT_PIDSHIFT,
			(hpc3_berr_stat & HPC3_BESTAT_CTYPE) ? "PIO" : "DMA",
			hpc3_berr_stat & HPC3_BESTAT_BLMASK);
		error |= 2;
	}
	if (extio_stat & EXTIO_EISA_BUSERR) {
		printk(KERN_ERR "EISA Bus Error\n");
		error |= 4;
	}
	if (cpu_err_stat & CPU_ERRMASK) {
		printk(KERN_ERR "CPU error 0x%x<%s%s%s%s%s%s> @ 0x%08x\n",
			cpu_err_stat,
			cpu_err_stat & SGIMC_CSTAT_RD ? "RD " : "",
			cpu_err_stat & SGIMC_CSTAT_PAR ? "PAR " : "",
			cpu_err_stat & SGIMC_CSTAT_ADDR ? "ADDR " : "",
			cpu_err_stat & SGIMC_CSTAT_SYSAD_PAR ? "SYSAD " : "",
			cpu_err_stat & SGIMC_CSTAT_SYSCMD_PAR ? "SYSCMD " : "",
			cpu_err_stat & SGIMC_CSTAT_BAD_DATA ? "BAD_DATA " : "",
			cpu_err_addr);
		error |= 8;
	}
	if (gio_err_stat & GIO_ERRMASK) {
		printk(KERN_ERR "GIO error 0x%x:<%s%s%s%s%s%s%s%s> @ 0x%08x\n",
			gio_err_stat,
			gio_err_stat & SGIMC_GSTAT_RD ? "RD " : "",
			gio_err_stat & SGIMC_GSTAT_WR ? "WR " : "",
			gio_err_stat & SGIMC_GSTAT_TIME ? "TIME " : "",
			gio_err_stat & SGIMC_GSTAT_PROM ? "PROM " : "",
			gio_err_stat & SGIMC_GSTAT_ADDR ? "ADDR " : "",
			gio_err_stat & SGIMC_GSTAT_BC ? "BC " : "",
			gio_err_stat & SGIMC_GSTAT_PIO_RD ? "PIO_RD " : "",
			gio_err_stat & SGIMC_GSTAT_PIO_WR ? "PIO_WR " : "",
			gio_err_addr);
		error |= 16;
	}
	if (!error)
		printk(KERN_ERR "MC: Hmm, didn't find any error condition.\n");
	else {
		printk(KERN_ERR "CP0: config %08x,  "
			"MC: cpuctrl0/1: %08x/%05x, giopar: %04x\n"
			"MC: cpu/gio_memacc: %08x/%05x, memcfg0/1: %08x/%08x\n",
			read_c0_config(),
			sgimc->cpuctrl0, sgimc->cpuctrl0, sgimc->giopar,
			sgimc->cmacc, sgimc->gmacc,
			sgimc->mconfig0, sgimc->mconfig1);
		print_cache_tags();
	}
	printk(KERN_ALERT "%s, epc == %0*lx, ra == %0*lx\n",
	       cause_excode_text(regs->cp0_cause),
	       field, regs->cp0_epc, field, regs->regs[31]);
}

/*
 * Check, whether MC's (virtual) DMA address caused the bus error.
 * See "Virtual DMA Specification", Draft 1.5, Feb 13 1992, SGI
 */

static int addr_is_ram(unsigned long addr, unsigned sz)
{
	int i;

	for (i = 0; i < boot_mem_map.nr_map; i++) {
		unsigned long a = boot_mem_map.map[i].addr;
		if (a <= addr && addr+sz <= a+boot_mem_map.map[i].size)
			return 1;
	}
	return 0;
}

static int check_microtlb(u32 hi, u32 lo, unsigned long vaddr)
{
	/* This is likely rather similar to correct code ;-) */

	vaddr &= 0x7fffffff; /* Doc. states that top bit is ignored */

	/* If tlb-entry is valid and VPN-high (bits [30:21] ?) matches... */
	if ((lo & 2) && (vaddr >> 21) == ((hi<<1) >> 22)) {
		u32 ctl = sgimc->dma_ctrl;
		if (ctl & 1) {
			unsigned int pgsz = (ctl & 2) ? 14:12; /* 16k:4k */
			/* PTEIndex is VPN-low (bits [22:14]/[20:12] ?) */
			unsigned long pte = (lo >> 6) << 12; /* PTEBase */
			pte += 8*((vaddr >> pgsz) & 0x1ff);
			if (addr_is_ram(pte, 8)) {
				/*
				 * Note: Since DMA hardware does look up
				 * translation on its own, this PTE *must*
				 * match the TLB/EntryLo-register format !
				 */
				unsigned long a = *(unsigned long *)
						PHYS_TO_XKSEG_UNCACHED(pte);
				a = (a & 0x3f) << 6; /* PFN */
				a += vaddr & ((1 << pgsz) - 1);
				return cpu_err_addr == a;
			}
		}
	}
	return 0;
}

static int check_vdma_memaddr(void)
{
	if (cpu_err_stat & CPU_ERRMASK) {
		u32 a = sgimc->maddronly;

		if (!(sgimc->dma_ctrl & 0x100)) /* Xlate-bit clear ? */
			return cpu_err_addr == a;

		if (check_microtlb(sgimc->dtlb_hi0, sgimc->dtlb_lo0, a) ||
		    check_microtlb(sgimc->dtlb_hi1, sgimc->dtlb_lo1, a) ||
		    check_microtlb(sgimc->dtlb_hi2, sgimc->dtlb_lo2, a) ||
		    check_microtlb(sgimc->dtlb_hi3, sgimc->dtlb_lo3, a))
			return 1;
	}
	return 0;
}

static int check_vdma_gioaddr(void)
{
	if (gio_err_stat & GIO_ERRMASK) {
		u32 a = sgimc->gio_dma_trans;
		a = (sgimc->gmaddronly & ~a) | (sgimc->gio_dma_sbits & a);
		return gio_err_addr == a;
	}
	return 0;
}

/*
 * MC sends an interrupt whenever bus or parity errors occur. In addition,
 * if the error happened during a CPU read, it also asserts the bus error
 * pin on the R4K. Code in bus error handler save the MC bus error registers
 * and then clear the interrupt when this happens.
 */

static int ip28_be_interrupt(const struct pt_regs *regs)
{
	int i;

	save_and_clear_buserr();
	/*
	 * Try to find out, whether we got here by a mispredicted speculative
	 * load/store operation.  If so, it's not fatal, we can go on.
	 */
	/* Any cause other than "Interrupt" (ExcCode 0) is fatal. */
	if (regs->cp0_cause & CAUSEF_EXCCODE)
		goto mips_be_fatal;

	/* Any cause other than "Bus error interrupt" (IP6) is weird. */
	if ((regs->cp0_cause & CAUSEF_IP6) != CAUSEF_IP6)
		goto mips_be_fatal;

	if (extio_stat & (EXTIO_HPC3_BUSERR | EXTIO_EISA_BUSERR))
		goto mips_be_fatal;

	/* Any state other than "Memory bus error" is fatal. */
	if (cpu_err_stat & CPU_ERRMASK & ~SGIMC_CSTAT_ADDR)
		goto mips_be_fatal;

	/* GIO errors other than timeouts are fatal */
	if (gio_err_stat & GIO_ERRMASK & ~SGIMC_GSTAT_TIME)
		goto mips_be_fatal;

	/*
	 * Now we have an asynchronous bus error, speculatively or DMA caused.
	 * Need to search all DMA descriptors for the error address.
	 */
	for (i = 0; i < sizeof(hpc3)/sizeof(struct hpc3_stat); ++i) {
		struct hpc3_stat *hp = (struct hpc3_stat *)&hpc3 + i;
		if ((cpu_err_stat & CPU_ERRMASK) &&
		    (cpu_err_addr == hp->ndptr || cpu_err_addr == hp->cbp))
			break;
		if ((gio_err_stat & GIO_ERRMASK) &&
		    (gio_err_addr == hp->ndptr || gio_err_addr == hp->cbp))
			break;
	}
	if (i < sizeof(hpc3)/sizeof(struct hpc3_stat)) {
		struct hpc3_stat *hp = (struct hpc3_stat *)&hpc3 + i;
		printk(KERN_ERR "at DMA addresses: HPC3 @ %08lx:"
		       " ctl %08x, ndp %08x, cbp %08x\n",
		       CPHYSADDR(hp->addr), hp->ctrl, hp->ndptr, hp->cbp);
		goto mips_be_fatal;
	}
	/* Check MC's virtual DMA stuff. */
	if (check_vdma_memaddr()) {
		printk(KERN_ERR "at GIO DMA: mem address 0x%08x.\n",
			sgimc->maddronly);
		goto mips_be_fatal;
	}
	if (check_vdma_gioaddr()) {
		printk(KERN_ERR "at GIO DMA: gio address 0x%08x.\n",
			sgimc->gmaddronly);
		goto mips_be_fatal;
	}
	/* A speculative bus error... */
	if (debug_be_interrupt) {
		print_buserr(regs);
		printk(KERN_ERR "discarded!\n");
	}
	return MIPS_BE_DISCARD;

mips_be_fatal:
	print_buserr(regs);
	return MIPS_BE_FATAL;
}

void ip22_be_interrupt(int irq)
{
	struct pt_regs *regs = get_irq_regs();

	count_be_interrupt++;

	if (ip28_be_interrupt(regs) != MIPS_BE_DISCARD) {
		/* Assume it would be too dangerous to continue ... */
		die_if_kernel("Oops", regs);
		force_sig(SIGBUS, current);
	} else if (debug_be_interrupt)
		show_regs((struct pt_regs *)regs);
}

static int ip28_be_handler(struct pt_regs *regs, int is_fixup)
{
	/*
	 * We arrive here only in the unusual case of do_be() invocation,
	 * i.e. by a bus error exception without a bus error interrupt.
	 */
	if (is_fixup) {
		count_be_is_fixup++;
		save_and_clear_buserr();
		return MIPS_BE_FIXUP;
	}
	count_be_handler++;
	return ip28_be_interrupt(regs);
}

void __init ip22_be_init(void)
{
	board_be_handler = ip28_be_handler;
}

int ip28_show_be_info(struct seq_file *m)
{
	seq_printf(m, "IP28 be fixups\t\t: %u\n", count_be_is_fixup);
	seq_printf(m, "IP28 be interrupts\t: %u\n", count_be_interrupt);
	seq_printf(m, "IP28 be handler\t\t: %u\n", count_be_handler);

	return 0;
}

static int __init debug_be_setup(char *str)
{
	debug_be_interrupt++;
	return 1;
}
__setup("ip28_debug_be", debug_be_setup);
