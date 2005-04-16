/*
 *  linux/arch/alpha/kernel/core_wildfire.c
 *
 *  Wildfire support.
 *
 *  Copyright (C) 2000 Andrea Arcangeli <andrea@suse.de> SuSE
 */

#define __EXTERN_INLINE inline
#include <asm/io.h>
#include <asm/core_wildfire.h>
#undef __EXTERN_INLINE

#include <linux/types.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/init.h>

#include <asm/ptrace.h>
#include <asm/smp.h>

#include "proto.h"
#include "pci_impl.h"

#define DEBUG_CONFIG 0
#define DEBUG_DUMP_REGS 0
#define DEBUG_DUMP_CONFIG 1

#if DEBUG_CONFIG
# define DBG_CFG(args)	printk args
#else
# define DBG_CFG(args)
#endif

#if DEBUG_DUMP_REGS
static void wildfire_dump_pci_regs(int qbbno, int hoseno);
static void wildfire_dump_pca_regs(int qbbno, int pcano);
static void wildfire_dump_qsa_regs(int qbbno);
static void wildfire_dump_qsd_regs(int qbbno);
static void wildfire_dump_iop_regs(int qbbno);
static void wildfire_dump_gp_regs(int qbbno);
#endif
#if DEBUG_DUMP_CONFIG
static void wildfire_dump_hardware_config(void);
#endif

unsigned char wildfire_hard_qbb_map[WILDFIRE_MAX_QBB];
unsigned char wildfire_soft_qbb_map[WILDFIRE_MAX_QBB];
#define QBB_MAP_EMPTY	0xff

unsigned long wildfire_hard_qbb_mask;
unsigned long wildfire_soft_qbb_mask;
unsigned long wildfire_gp_mask;
unsigned long wildfire_hs_mask;
unsigned long wildfire_iop_mask;
unsigned long wildfire_ior_mask;
unsigned long wildfire_pca_mask;
unsigned long wildfire_cpu_mask;
unsigned long wildfire_mem_mask;

void __init
wildfire_init_hose(int qbbno, int hoseno)
{
	struct pci_controller *hose;
	wildfire_pci *pci;

	hose = alloc_pci_controller();
	hose->io_space = alloc_resource();
	hose->mem_space = alloc_resource();

        /* This is for userland consumption. */
        hose->sparse_mem_base = 0;
        hose->sparse_io_base  = 0;
        hose->dense_mem_base  = WILDFIRE_MEM(qbbno, hoseno);
        hose->dense_io_base   = WILDFIRE_IO(qbbno, hoseno);

	hose->config_space_base = WILDFIRE_CONF(qbbno, hoseno);
	hose->index = (qbbno << 3) + hoseno;

	hose->io_space->start = WILDFIRE_IO(qbbno, hoseno) - WILDFIRE_IO_BIAS;
	hose->io_space->end = hose->io_space->start + WILDFIRE_IO_SPACE - 1;
	hose->io_space->name = pci_io_names[hoseno];
	hose->io_space->flags = IORESOURCE_IO;

	hose->mem_space->start = WILDFIRE_MEM(qbbno, hoseno)-WILDFIRE_MEM_BIAS;
	hose->mem_space->end = hose->mem_space->start + 0xffffffff;
	hose->mem_space->name = pci_mem_names[hoseno];
	hose->mem_space->flags = IORESOURCE_MEM;

	if (request_resource(&ioport_resource, hose->io_space) < 0)
		printk(KERN_ERR "Failed to request IO on qbb %d hose %d\n",
		       qbbno, hoseno);
	if (request_resource(&iomem_resource, hose->mem_space) < 0)
		printk(KERN_ERR "Failed to request MEM on qbb %d hose %d\n",
		       qbbno, hoseno);

#if DEBUG_DUMP_REGS
	wildfire_dump_pci_regs(qbbno, hoseno);
#endif

        /*
         * Set up the PCI to main memory translation windows.
         *
         * Note: Window 3 is scatter-gather only
         * 
         * Window 0 is scatter-gather 8MB at 8MB (for isa)
	 * Window 1 is direct access 1GB at 1GB
	 * Window 2 is direct access 1GB at 2GB
         * Window 3 is scatter-gather 128MB at 3GB
         * ??? We ought to scale window 3 memory.
         *
         */
        hose->sg_isa = iommu_arena_new(hose, 0x00800000, 0x00800000, 0);
        hose->sg_pci = iommu_arena_new(hose, 0xc0000000, 0x08000000, 0);

	pci = WILDFIRE_pci(qbbno, hoseno);

	pci->pci_window[0].wbase.csr = hose->sg_isa->dma_base | 3;
	pci->pci_window[0].wmask.csr = (hose->sg_isa->size - 1) & 0xfff00000;
	pci->pci_window[0].tbase.csr = virt_to_phys(hose->sg_isa->ptes);

	pci->pci_window[1].wbase.csr = 0x40000000 | 1;
	pci->pci_window[1].wmask.csr = (0x40000000 -1) & 0xfff00000;
	pci->pci_window[1].tbase.csr = 0;

	pci->pci_window[2].wbase.csr = 0x80000000 | 1;
	pci->pci_window[2].wmask.csr = (0x40000000 -1) & 0xfff00000;
	pci->pci_window[2].tbase.csr = 0x40000000;

	pci->pci_window[3].wbase.csr = hose->sg_pci->dma_base | 3;
	pci->pci_window[3].wmask.csr = (hose->sg_pci->size - 1) & 0xfff00000;
	pci->pci_window[3].tbase.csr = virt_to_phys(hose->sg_pci->ptes);

	wildfire_pci_tbi(hose, 0, 0); /* Flush TLB at the end. */
}

void __init
wildfire_init_pca(int qbbno, int pcano)
{

	/* Test for PCA existence first. */
	if (!WILDFIRE_PCA_EXISTS(qbbno, pcano))
	    return;

#if DEBUG_DUMP_REGS
	wildfire_dump_pca_regs(qbbno, pcano);
#endif

	/* Do both hoses of the PCA. */
	wildfire_init_hose(qbbno, (pcano << 1) + 0);
	wildfire_init_hose(qbbno, (pcano << 1) + 1);
}

void __init
wildfire_init_qbb(int qbbno)
{
	int pcano;

	/* Test for QBB existence first. */
	if (!WILDFIRE_QBB_EXISTS(qbbno))
		return;

#if DEBUG_DUMP_REGS
	wildfire_dump_qsa_regs(qbbno);
	wildfire_dump_qsd_regs(qbbno);
	wildfire_dump_iop_regs(qbbno);
	wildfire_dump_gp_regs(qbbno);
#endif

	/* Init all PCAs here. */
	for (pcano = 0; pcano < WILDFIRE_PCA_PER_QBB; pcano++) {
		wildfire_init_pca(qbbno, pcano);
	}
}

void __init
wildfire_hardware_probe(void)
{
	unsigned long temp;
	unsigned int hard_qbb, soft_qbb;
	wildfire_fast_qsd *fast = WILDFIRE_fast_qsd();
	wildfire_qsd *qsd;
	wildfire_qsa *qsa;
	wildfire_iop *iop;
	wildfire_gp *gp;
	wildfire_ne *ne;
	wildfire_fe *fe;
	int i;

	temp = fast->qsd_whami.csr;
#if 0
	printk(KERN_ERR "fast QSD_WHAMI at base %p is 0x%lx\n", fast, temp);
#endif

	hard_qbb = (temp >> 8) & 7;
	soft_qbb = (temp >> 4) & 7;

	/* Init the HW configuration variables. */
	wildfire_hard_qbb_mask = (1 << hard_qbb);
	wildfire_soft_qbb_mask = (1 << soft_qbb);

	wildfire_gp_mask = 0;
	wildfire_hs_mask = 0;
	wildfire_iop_mask = 0;
	wildfire_ior_mask = 0;
	wildfire_pca_mask = 0;

	wildfire_cpu_mask = 0;
	wildfire_mem_mask = 0;

	memset(wildfire_hard_qbb_map, QBB_MAP_EMPTY, WILDFIRE_MAX_QBB);
	memset(wildfire_soft_qbb_map, QBB_MAP_EMPTY, WILDFIRE_MAX_QBB);

	/* First, determine which QBBs are present. */
	qsa = WILDFIRE_qsa(soft_qbb);

	temp = qsa->qsa_qbb_id.csr;
#if 0
	printk(KERN_ERR "QSA_QBB_ID at base %p is 0x%lx\n", qsa, temp);
#endif

	if (temp & 0x40) /* Is there an HS? */
		wildfire_hs_mask = 1;

	if (temp & 0x20) { /* Is there a GP? */
		gp = WILDFIRE_gp(soft_qbb);
		temp = 0;
		for (i = 0; i < 4; i++) {
			temp |= gp->gpa_qbb_map[i].csr << (i * 8);
#if 0
			printk(KERN_ERR "GPA_QBB_MAP[%d] at base %p is 0x%lx\n",
			       i, gp, temp);
#endif
		}

		for (hard_qbb = 0; hard_qbb < WILDFIRE_MAX_QBB; hard_qbb++) {
			if (temp & 8) { /* Is there a QBB? */
				soft_qbb = temp & 7;
				wildfire_hard_qbb_mask |= (1 << hard_qbb);
				wildfire_soft_qbb_mask |= (1 << soft_qbb);
			}
			temp >>= 4;
		}
		wildfire_gp_mask = wildfire_soft_qbb_mask;
        }

	/* Next determine each QBBs resources. */
	for (soft_qbb = 0; soft_qbb < WILDFIRE_MAX_QBB; soft_qbb++) {
	    if (WILDFIRE_QBB_EXISTS(soft_qbb)) {
	        qsd = WILDFIRE_qsd(soft_qbb);
		temp = qsd->qsd_whami.csr;
#if 0
	printk(KERN_ERR "QSD_WHAMI at base %p is 0x%lx\n", qsd, temp);
#endif
		hard_qbb = (temp >> 8) & 7;
		wildfire_hard_qbb_map[hard_qbb] = soft_qbb;
		wildfire_soft_qbb_map[soft_qbb] = hard_qbb;

		qsa = WILDFIRE_qsa(soft_qbb);
		temp = qsa->qsa_qbb_pop[0].csr;
#if 0
	printk(KERN_ERR "QSA_QBB_POP_0 at base %p is 0x%lx\n", qsa, temp);
#endif
		wildfire_cpu_mask |= ((temp >> 0) & 0xf) << (soft_qbb << 2);
		wildfire_mem_mask |= ((temp >> 4) & 0xf) << (soft_qbb << 2);

		temp = qsa->qsa_qbb_pop[1].csr;
#if 0
	printk(KERN_ERR "QSA_QBB_POP_1 at base %p is 0x%lx\n", qsa, temp);
#endif
		wildfire_iop_mask |= (1 << soft_qbb);
		wildfire_ior_mask |= ((temp >> 4) & 0xf) << (soft_qbb << 2);

		temp = qsa->qsa_qbb_id.csr;
#if 0
	printk(KERN_ERR "QSA_QBB_ID at %p is 0x%lx\n", qsa, temp);
#endif
		if (temp & 0x20)
		    wildfire_gp_mask |= (1 << soft_qbb);

		/* Probe for PCA existence here. */
		for (i = 0; i < WILDFIRE_PCA_PER_QBB; i++) {
		    iop = WILDFIRE_iop(soft_qbb);
		    ne = WILDFIRE_ne(soft_qbb, i);
		    fe = WILDFIRE_fe(soft_qbb, i);

		    if ((iop->iop_hose[i].init.csr & 1) == 1 &&
			((ne->ne_what_am_i.csr & 0xf00000300UL) == 0x100000300UL) &&
			((fe->fe_what_am_i.csr & 0xf00000300UL) == 0x100000200UL))
		    {
		        wildfire_pca_mask |= 1 << ((soft_qbb << 2) + i);
		    }
		}

	    }
	}
#if DEBUG_DUMP_CONFIG
	wildfire_dump_hardware_config();
#endif
}

void __init
wildfire_init_arch(void)
{
	int qbbno;

	/* With multiple PCI buses, we play with I/O as physical addrs.  */
	ioport_resource.end = ~0UL;


	/* Probe the hardware for info about configuration. */
	wildfire_hardware_probe();

	/* Now init all the found QBBs. */
	for (qbbno = 0; qbbno < WILDFIRE_MAX_QBB; qbbno++) {
		wildfire_init_qbb(qbbno);
	}

	/* Normal direct PCI DMA mapping. */ 
	__direct_map_base = 0x40000000UL;
	__direct_map_size = 0x80000000UL;
}

void
wildfire_machine_check(unsigned long vector, unsigned long la_ptr,
		       struct pt_regs * regs)
{
	mb();
	mb();  /* magic */
	draina();
	/* FIXME: clear pci errors */
	wrmces(0x7);
	mb();

	process_mcheck_info(vector, la_ptr, regs, "WILDFIRE",
			    mcheck_expected(smp_processor_id()));
}

void
wildfire_kill_arch(int mode)
{
}

void
wildfire_pci_tbi(struct pci_controller *hose, dma_addr_t start, dma_addr_t end)
{
	int qbbno = hose->index >> 3;
	int hoseno = hose->index & 7;
	wildfire_pci *pci = WILDFIRE_pci(qbbno, hoseno);

	mb();
	pci->pci_flush_tlb.csr; /* reading does the trick */
}

static int
mk_conf_addr(struct pci_bus *pbus, unsigned int device_fn, int where,
	     unsigned long *pci_addr, unsigned char *type1)
{
	struct pci_controller *hose = pbus->sysdata;
	unsigned long addr;
	u8 bus = pbus->number;

	DBG_CFG(("mk_conf_addr(bus=%d ,device_fn=0x%x, where=0x%x, "
		 "pci_addr=0x%p, type1=0x%p)\n",
		 bus, device_fn, where, pci_addr, type1));

	if (!pbus->parent) /* No parent means peer PCI bus. */
		bus = 0;
	*type1 = (bus != 0);

	addr = (bus << 16) | (device_fn << 8) | where;
	addr |= hose->config_space_base;
		
	*pci_addr = addr;
	DBG_CFG(("mk_conf_addr: returning pci_addr 0x%lx\n", addr));
	return 0;
}

static int 
wildfire_read_config(struct pci_bus *bus, unsigned int devfn, int where,
		     int size, u32 *value)
{
	unsigned long addr;
	unsigned char type1;

	if (mk_conf_addr(bus, devfn, where, &addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	switch (size) {
	case 1:
		*value = __kernel_ldbu(*(vucp)addr);
		break;
	case 2:
		*value = __kernel_ldwu(*(vusp)addr);
		break;
	case 4:
		*value = *(vuip)addr;
		break;
	}

	return PCIBIOS_SUCCESSFUL;
}

static int 
wildfire_write_config(struct pci_bus *bus, unsigned int devfn, int where,
		      int size, u32 value)
{
	unsigned long addr;
	unsigned char type1;

	if (mk_conf_addr(bus, devfn, where, &addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	switch (size) {
	case 1:
		__kernel_stb(value, *(vucp)addr);
		mb();
		__kernel_ldbu(*(vucp)addr);
		break;
	case 2:
		__kernel_stw(value, *(vusp)addr);
		mb();
		__kernel_ldwu(*(vusp)addr);
		break;
	case 4:
		*(vuip)addr = value;
		mb();
		*(vuip)addr;
		break;
	}

	return PCIBIOS_SUCCESSFUL;
}

struct pci_ops wildfire_pci_ops = 
{
	.read =		wildfire_read_config,
	.write =	wildfire_write_config,
};


/*
 * NUMA Support
 */
int wildfire_pa_to_nid(unsigned long pa)
{
	return pa >> 36;
}

int wildfire_cpuid_to_nid(int cpuid)
{
	/* assume 4 CPUs per node */
	return cpuid >> 2;
}

unsigned long wildfire_node_mem_start(int nid)
{
	/* 64GB per node */
	return (unsigned long)nid * (64UL * 1024 * 1024 * 1024);
}

unsigned long wildfire_node_mem_size(int nid)
{
	/* 64GB per node */
	return 64UL * 1024 * 1024 * 1024;
}

#if DEBUG_DUMP_REGS

static void __init
wildfire_dump_pci_regs(int qbbno, int hoseno)
{
	wildfire_pci *pci = WILDFIRE_pci(qbbno, hoseno);
	int i;

	printk(KERN_ERR "PCI registers for QBB %d hose %d (%p)\n",
	       qbbno, hoseno, pci);

	printk(KERN_ERR " PCI_IO_ADDR_EXT: 0x%16lx\n",
	       pci->pci_io_addr_ext.csr);
	printk(KERN_ERR " PCI_CTRL:        0x%16lx\n", pci->pci_ctrl.csr);
	printk(KERN_ERR " PCI_ERR_SUM:     0x%16lx\n", pci->pci_err_sum.csr);
	printk(KERN_ERR " PCI_ERR_ADDR:    0x%16lx\n", pci->pci_err_addr.csr);
	printk(KERN_ERR " PCI_STALL_CNT:   0x%16lx\n", pci->pci_stall_cnt.csr);
	printk(KERN_ERR " PCI_PEND_INT:    0x%16lx\n", pci->pci_pend_int.csr);
	printk(KERN_ERR " PCI_SENT_INT:    0x%16lx\n", pci->pci_sent_int.csr);

	printk(KERN_ERR " DMA window registers for QBB %d hose %d (%p)\n",
	       qbbno, hoseno, pci);
	for (i = 0; i < 4; i++) {
		printk(KERN_ERR "  window %d: 0x%16lx 0x%16lx 0x%16lx\n", i,
		       pci->pci_window[i].wbase.csr,
		       pci->pci_window[i].wmask.csr,
		       pci->pci_window[i].tbase.csr);
	}
	printk(KERN_ERR "\n");
}

static void __init
wildfire_dump_pca_regs(int qbbno, int pcano)
{
	wildfire_pca *pca = WILDFIRE_pca(qbbno, pcano);
	int i;

	printk(KERN_ERR "PCA registers for QBB %d PCA %d (%p)\n",
	       qbbno, pcano, pca);

	printk(KERN_ERR " PCA_WHAT_AM_I: 0x%16lx\n", pca->pca_what_am_i.csr);
	printk(KERN_ERR " PCA_ERR_SUM:   0x%16lx\n", pca->pca_err_sum.csr);
	printk(KERN_ERR " PCA_PEND_INT:  0x%16lx\n", pca->pca_pend_int.csr);
	printk(KERN_ERR " PCA_SENT_INT:  0x%16lx\n", pca->pca_sent_int.csr);
	printk(KERN_ERR " PCA_STDIO_EL:  0x%16lx\n",
	       pca->pca_stdio_edge_level.csr);

	printk(KERN_ERR " PCA target registers for QBB %d PCA %d (%p)\n",
	       qbbno, pcano, pca);
	for (i = 0; i < 4; i++) {
	  printk(KERN_ERR "  target %d: 0x%16lx 0x%16lx\n", i,
		       pca->pca_int[i].target.csr,
		       pca->pca_int[i].enable.csr);
	}

	printk(KERN_ERR "\n");
}

static void __init
wildfire_dump_qsa_regs(int qbbno)
{
	wildfire_qsa *qsa = WILDFIRE_qsa(qbbno);
	int i;

	printk(KERN_ERR "QSA registers for QBB %d (%p)\n", qbbno, qsa);

	printk(KERN_ERR " QSA_QBB_ID:      0x%16lx\n", qsa->qsa_qbb_id.csr);
	printk(KERN_ERR " QSA_PORT_ENA:    0x%16lx\n", qsa->qsa_port_ena.csr);
	printk(KERN_ERR " QSA_REF_INT:     0x%16lx\n", qsa->qsa_ref_int.csr);

	for (i = 0; i < 5; i++)
		printk(KERN_ERR " QSA_CONFIG_%d:    0x%16lx\n",
		       i, qsa->qsa_config[i].csr);

	for (i = 0; i < 2; i++)
		printk(KERN_ERR " QSA_QBB_POP_%d:   0x%16lx\n",
		       i, qsa->qsa_qbb_pop[0].csr);

	printk(KERN_ERR "\n");
}

static void __init
wildfire_dump_qsd_regs(int qbbno)
{
	wildfire_qsd *qsd = WILDFIRE_qsd(qbbno);

	printk(KERN_ERR "QSD registers for QBB %d (%p)\n", qbbno, qsd);

	printk(KERN_ERR " QSD_WHAMI:         0x%16lx\n", qsd->qsd_whami.csr);
	printk(KERN_ERR " QSD_REV:           0x%16lx\n", qsd->qsd_rev.csr);
	printk(KERN_ERR " QSD_PORT_PRESENT:  0x%16lx\n",
	       qsd->qsd_port_present.csr);
	printk(KERN_ERR " QSD_PORT_ACTUVE:   0x%16lx\n",
	       qsd->qsd_port_active.csr);
	printk(KERN_ERR " QSD_FAULT_ENA:     0x%16lx\n",
	       qsd->qsd_fault_ena.csr);
	printk(KERN_ERR " QSD_CPU_INT_ENA:   0x%16lx\n",
	       qsd->qsd_cpu_int_ena.csr);
	printk(KERN_ERR " QSD_MEM_CONFIG:    0x%16lx\n",
	       qsd->qsd_mem_config.csr);
	printk(KERN_ERR " QSD_ERR_SUM:       0x%16lx\n",
	       qsd->qsd_err_sum.csr);

	printk(KERN_ERR "\n");
}

static void __init
wildfire_dump_iop_regs(int qbbno)
{
	wildfire_iop *iop = WILDFIRE_iop(qbbno);
	int i;

	printk(KERN_ERR "IOP registers for QBB %d (%p)\n", qbbno, iop);

	printk(KERN_ERR " IOA_CONFIG:          0x%16lx\n", iop->ioa_config.csr);
	printk(KERN_ERR " IOD_CONFIG:          0x%16lx\n", iop->iod_config.csr);
	printk(KERN_ERR " IOP_SWITCH_CREDITS:  0x%16lx\n",
	       iop->iop_switch_credits.csr);
	printk(KERN_ERR " IOP_HOSE_CREDITS:    0x%16lx\n",
	       iop->iop_hose_credits.csr);

	for (i = 0; i < 4; i++) 
		printk(KERN_ERR " IOP_HOSE_%d_INIT:     0x%16lx\n",
		       i, iop->iop_hose[i].init.csr);
	for (i = 0; i < 4; i++) 
		printk(KERN_ERR " IOP_DEV_INT_TARGET_%d: 0x%16lx\n",
		       i, iop->iop_dev_int[i].target.csr);

	printk(KERN_ERR "\n");
}

static void __init
wildfire_dump_gp_regs(int qbbno)
{
	wildfire_gp *gp = WILDFIRE_gp(qbbno);
	int i;

	printk(KERN_ERR "GP registers for QBB %d (%p)\n", qbbno, gp);
	for (i = 0; i < 4; i++) 
		printk(KERN_ERR " GPA_QBB_MAP_%d:     0x%16lx\n",
		       i, gp->gpa_qbb_map[i].csr);

	printk(KERN_ERR " GPA_MEM_POP_MAP:   0x%16lx\n",
	       gp->gpa_mem_pop_map.csr);
	printk(KERN_ERR " GPA_SCRATCH:       0x%16lx\n", gp->gpa_scratch.csr);
	printk(KERN_ERR " GPA_DIAG:          0x%16lx\n", gp->gpa_diag.csr);
	printk(KERN_ERR " GPA_CONFIG_0:      0x%16lx\n", gp->gpa_config_0.csr);
	printk(KERN_ERR " GPA_INIT_ID:       0x%16lx\n", gp->gpa_init_id.csr);
	printk(KERN_ERR " GPA_CONFIG_2:      0x%16lx\n", gp->gpa_config_2.csr);

	printk(KERN_ERR "\n");
}
#endif /* DUMP_REGS */

#if DEBUG_DUMP_CONFIG
static void __init
wildfire_dump_hardware_config(void)
{
	int i;

	printk(KERN_ERR "Probed Hardware Configuration\n");

	printk(KERN_ERR " hard_qbb_mask:  0x%16lx\n", wildfire_hard_qbb_mask);
	printk(KERN_ERR " soft_qbb_mask:  0x%16lx\n", wildfire_soft_qbb_mask);

	printk(KERN_ERR " gp_mask:        0x%16lx\n", wildfire_gp_mask);
	printk(KERN_ERR " hs_mask:        0x%16lx\n", wildfire_hs_mask);
	printk(KERN_ERR " iop_mask:       0x%16lx\n", wildfire_iop_mask);
	printk(KERN_ERR " ior_mask:       0x%16lx\n", wildfire_ior_mask);
	printk(KERN_ERR " pca_mask:       0x%16lx\n", wildfire_pca_mask);

	printk(KERN_ERR " cpu_mask:       0x%16lx\n", wildfire_cpu_mask);
	printk(KERN_ERR " mem_mask:       0x%16lx\n", wildfire_mem_mask);

	printk(" hard_qbb_map: ");
	for (i = 0; i < WILDFIRE_MAX_QBB; i++)
	    if (wildfire_hard_qbb_map[i] == QBB_MAP_EMPTY)
		printk("--- ");
	    else
		printk("%3d ", wildfire_hard_qbb_map[i]);
	printk("\n");

	printk(" soft_qbb_map: ");
	for (i = 0; i < WILDFIRE_MAX_QBB; i++)
	    if (wildfire_soft_qbb_map[i] == QBB_MAP_EMPTY)
		printk("--- ");
	    else
		printk("%3d ", wildfire_soft_qbb_map[i]);
	printk("\n");
}
#endif /* DUMP_CONFIG */
