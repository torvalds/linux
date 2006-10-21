/*
 * PPC440GX system library
 *
 * Eugene Surovegin <eugene.surovegin@zultys.com> or <ebs@ebshome.net>
 * Copyright (c) 2003 - 2006 Zultys Technologies
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <asm/ibm44x.h>
#include <asm/mmu.h>
#include <asm/processor.h>
#include <syslib/ibm440gx_common.h>

/*
 * Calculate 440GX clocks
 */
static inline u32 __fix_zero(u32 v, u32 def){
	return v ? v : def;
}

void __init ibm440gx_get_clocks(struct ibm44x_clocks* p, unsigned int sys_clk,
	unsigned int ser_clk)
{
	u32 pllc  = CPR_READ(DCRN_CPR_PLLC);
	u32 plld  = CPR_READ(DCRN_CPR_PLLD);
	u32 uart0 = SDR_READ(DCRN_SDR_UART0);
	u32 uart1 = SDR_READ(DCRN_SDR_UART1);
#ifdef CONFIG_440EP
	u32 uart2 = SDR_READ(DCRN_SDR_UART2);
	u32 uart3 = SDR_READ(DCRN_SDR_UART3);
#endif

	/* Dividers */
	u32 fbdv   = __fix_zero((plld >> 24) & 0x1f, 32);
	u32 fwdva  = __fix_zero((plld >> 16) & 0xf, 16);
	u32 fwdvb  = __fix_zero((plld >> 8) & 7, 8);
	u32 lfbdv  = __fix_zero(plld & 0x3f, 64);
	u32 pradv0 = __fix_zero((CPR_READ(DCRN_CPR_PRIMAD) >> 24) & 7, 8);
	u32 prbdv0 = __fix_zero((CPR_READ(DCRN_CPR_PRIMBD) >> 24) & 7, 8);
	u32 opbdv0 = __fix_zero((CPR_READ(DCRN_CPR_OPBD) >> 24) & 3, 4);
	u32 perdv0 = __fix_zero((CPR_READ(DCRN_CPR_PERD) >> 24) & 3, 4);

	/* Input clocks for primary dividers */
	u32 clk_a, clk_b;

	if (pllc & 0x40000000){
		u32 m;

		/* Feedback path */
		switch ((pllc >> 24) & 7){
		case 0:
			/* PLLOUTx */
			m = ((pllc & 0x20000000) ? fwdvb : fwdva) * lfbdv;
			break;
		case 1:
			/* CPU */
			m = fwdva * pradv0;
			break;
		case 5:
			/* PERClk */
			m = fwdvb * prbdv0 * opbdv0 * perdv0;
			break;
		default:
			printk(KERN_EMERG "invalid PLL feedback source\n");
			goto bypass;
		}
		m *= fbdv;
		p->vco = sys_clk * m;
		clk_a = p->vco / fwdva;
		clk_b = p->vco / fwdvb;
	}
	else {
bypass:
		/* Bypass system PLL */
		p->vco = 0;
		clk_a = clk_b = sys_clk;
	}

	p->cpu = clk_a / pradv0;
	p->plb = clk_b / prbdv0;
	p->opb = p->plb / opbdv0;
	p->ebc = p->opb / perdv0;

	/* UARTs clock */
	if (uart0 & 0x00800000)
		p->uart0 = ser_clk;
	else
		p->uart0 = p->plb / __fix_zero(uart0 & 0xff, 256);

	if (uart1 & 0x00800000)
		p->uart1 = ser_clk;
	else
		p->uart1 = p->plb / __fix_zero(uart1 & 0xff, 256);
#ifdef CONFIG_440EP
	if (uart2 & 0x00800000)
		p->uart2 = ser_clk;
	else
		p->uart2 = p->plb / __fix_zero(uart2 & 0xff, 256);

	if (uart3 & 0x00800000)
		p->uart3 = ser_clk;
	else
		p->uart3 = p->plb / __fix_zero(uart3 & 0xff, 256);
#endif
}

/* Issue L2C diagnostic command */
static inline u32 l2c_diag(u32 addr)
{
	mtdcr(DCRN_L2C0_ADDR, addr);
	mtdcr(DCRN_L2C0_CMD, L2C_CMD_DIAG);
	while (!(mfdcr(DCRN_L2C0_SR) & L2C_SR_CC)) ;
	return mfdcr(DCRN_L2C0_DATA);
}

static irqreturn_t l2c_error_handler(int irq, void* dev)
{
	u32 sr = mfdcr(DCRN_L2C0_SR);
	if (sr & L2C_SR_CPE){
		/* Read cache trapped address */
		u32 addr = l2c_diag(0x42000000);
		printk(KERN_EMERG "L2C: Cache Parity Error, addr[16:26] = 0x%08x\n", addr);
	}
	if (sr & L2C_SR_TPE){
		/* Read tag trapped address */
		u32 addr = l2c_diag(0x82000000) >> 16;
		printk(KERN_EMERG "L2C: Tag Parity Error, addr[16:26] = 0x%08x\n", addr);
	}

	/* Clear parity errors */
	if (sr & (L2C_SR_CPE | L2C_SR_TPE)){
		mtdcr(DCRN_L2C0_ADDR, 0);
		mtdcr(DCRN_L2C0_CMD, L2C_CMD_CCP | L2C_CMD_CTE);
	} else
		printk(KERN_EMERG "L2C: LRU error\n");

	return IRQ_HANDLED;
}

/* Enable L2 cache */
void __init ibm440gx_l2c_enable(void){
	u32 r;
	unsigned long flags;

	/* Install error handler */
	if (request_irq(87, l2c_error_handler, IRQF_DISABLED, "L2C", 0) < 0){
		printk(KERN_ERR "Cannot install L2C error handler, cache is not enabled\n");
		return;
	}

	local_irq_save(flags);
	asm volatile ("sync" ::: "memory");

	/* Disable SRAM */
	mtdcr(DCRN_SRAM0_DPC,   mfdcr(DCRN_SRAM0_DPC)   & ~SRAM_DPC_ENABLE);
	mtdcr(DCRN_SRAM0_SB0CR, mfdcr(DCRN_SRAM0_SB0CR) & ~SRAM_SBCR_BU_MASK);
	mtdcr(DCRN_SRAM0_SB1CR, mfdcr(DCRN_SRAM0_SB1CR) & ~SRAM_SBCR_BU_MASK);
	mtdcr(DCRN_SRAM0_SB2CR, mfdcr(DCRN_SRAM0_SB2CR) & ~SRAM_SBCR_BU_MASK);
	mtdcr(DCRN_SRAM0_SB3CR, mfdcr(DCRN_SRAM0_SB3CR) & ~SRAM_SBCR_BU_MASK);

	/* Enable L2_MODE without ICU/DCU */
	r = mfdcr(DCRN_L2C0_CFG) & ~(L2C_CFG_ICU | L2C_CFG_DCU | L2C_CFG_SS_MASK);
	r |= L2C_CFG_L2M | L2C_CFG_SS_256;
	mtdcr(DCRN_L2C0_CFG, r);

	mtdcr(DCRN_L2C0_ADDR, 0);

	/* Hardware Clear Command */
	mtdcr(DCRN_L2C0_CMD, L2C_CMD_HCC);
	while (!(mfdcr(DCRN_L2C0_SR) & L2C_SR_CC)) ;

	/* Clear Cache Parity and Tag Errors */
	mtdcr(DCRN_L2C0_CMD, L2C_CMD_CCP | L2C_CMD_CTE);

	/* Enable 64G snoop region starting at 0 */
	r = mfdcr(DCRN_L2C0_SNP0) & ~(L2C_SNP_BA_MASK | L2C_SNP_SSR_MASK);
	r |= L2C_SNP_SSR_32G | L2C_SNP_ESR;
	mtdcr(DCRN_L2C0_SNP0, r);

	r = mfdcr(DCRN_L2C0_SNP1) & ~(L2C_SNP_BA_MASK | L2C_SNP_SSR_MASK);
	r |= 0x80000000 | L2C_SNP_SSR_32G | L2C_SNP_ESR;
	mtdcr(DCRN_L2C0_SNP1, r);

	asm volatile ("sync" ::: "memory");

	/* Enable ICU/DCU ports */
	r = mfdcr(DCRN_L2C0_CFG);
	r &= ~(L2C_CFG_DCW_MASK | L2C_CFG_PMUX_MASK | L2C_CFG_PMIM | L2C_CFG_TPEI
		| L2C_CFG_CPEI | L2C_CFG_NAM | L2C_CFG_NBRM);
	r |= L2C_CFG_ICU | L2C_CFG_DCU | L2C_CFG_TPC | L2C_CFG_CPC | L2C_CFG_FRAN
		| L2C_CFG_CPIM | L2C_CFG_TPIM | L2C_CFG_LIM | L2C_CFG_SMCM;
	mtdcr(DCRN_L2C0_CFG, r);

	asm volatile ("sync; isync" ::: "memory");
	local_irq_restore(flags);
}

/* Disable L2 cache */
void __init ibm440gx_l2c_disable(void){
	u32 r;
	unsigned long flags;

	local_irq_save(flags);
	asm volatile ("sync" ::: "memory");

	/* Disable L2C mode */
	r = mfdcr(DCRN_L2C0_CFG) & ~(L2C_CFG_L2M | L2C_CFG_ICU | L2C_CFG_DCU);
	mtdcr(DCRN_L2C0_CFG, r);

	/* Enable SRAM */
	mtdcr(DCRN_SRAM0_DPC, mfdcr(DCRN_SRAM0_DPC) | SRAM_DPC_ENABLE);
	mtdcr(DCRN_SRAM0_SB0CR,
	      SRAM_SBCR_BAS0 | SRAM_SBCR_BS_64KB | SRAM_SBCR_BU_RW);
	mtdcr(DCRN_SRAM0_SB1CR,
	      SRAM_SBCR_BAS1 | SRAM_SBCR_BS_64KB | SRAM_SBCR_BU_RW);
	mtdcr(DCRN_SRAM0_SB2CR,
	      SRAM_SBCR_BAS2 | SRAM_SBCR_BS_64KB | SRAM_SBCR_BU_RW);
	mtdcr(DCRN_SRAM0_SB3CR,
	      SRAM_SBCR_BAS3 | SRAM_SBCR_BS_64KB | SRAM_SBCR_BU_RW);

	asm volatile ("sync; isync" ::: "memory");
	local_irq_restore(flags);
}

void __init ibm440gx_l2c_setup(struct ibm44x_clocks* p)
{
	/* Disable L2C on rev.A, rev.B and 800MHz version of rev.C,
	   enable it on all other revisions
	 */
	if (strcmp(cur_cpu_spec->cpu_name, "440GX Rev. A") == 0 ||
			strcmp(cur_cpu_spec->cpu_name, "440GX Rev. B") == 0
			|| (strcmp(cur_cpu_spec->cpu_name, "440GX Rev. C")
				== 0 && p->cpu > 667000000))
		ibm440gx_l2c_disable();
	else
		ibm440gx_l2c_enable();
}

int __init ibm440gx_get_eth_grp(void)
{
	return (SDR_READ(DCRN_SDR_PFC1) & DCRN_SDR_PFC1_EPS) >> DCRN_SDR_PFC1_EPS_SHIFT;
}

void __init ibm440gx_set_eth_grp(int group)
{
	SDR_WRITE(DCRN_SDR_PFC1, (SDR_READ(DCRN_SDR_PFC1) & ~DCRN_SDR_PFC1_EPS) | (group << DCRN_SDR_PFC1_EPS_SHIFT));
}

void __init ibm440gx_tah_enable(void)
{
	/* Enable TAH0 and TAH1 */
	SDR_WRITE(DCRN_SDR_MFR,SDR_READ(DCRN_SDR_MFR) &
			~DCRN_SDR_MFR_TAH0);
	SDR_WRITE(DCRN_SDR_MFR,SDR_READ(DCRN_SDR_MFR) &
			~DCRN_SDR_MFR_TAH1);
}

int ibm440gx_show_cpuinfo(struct seq_file *m){

	u32 l2c_cfg = mfdcr(DCRN_L2C0_CFG);
	const char* s;
	if (l2c_cfg & L2C_CFG_L2M){
	    switch (l2c_cfg & (L2C_CFG_ICU | L2C_CFG_DCU)){
		case L2C_CFG_ICU: s = "I-Cache only";    break;
		case L2C_CFG_DCU: s = "D-Cache only";    break;
		default:	  s = "I-Cache/D-Cache"; break;
	    }
	}
	else
	    s = "disabled";

	seq_printf(m, "L2-Cache\t: %s (0x%08x 0x%08x)\n", s,
	    l2c_cfg, mfdcr(DCRN_L2C0_SR));

	return 0;
}

void __init ibm440gx_platform_init(unsigned long r3, unsigned long r4,
				   unsigned long r5, unsigned long r6,
				   unsigned long r7)
{
	/* Erratum 440_43 workaround, disable L1 cache parity checking */
	if (!strcmp(cur_cpu_spec->cpu_name, "440GX Rev. C") ||
	    !strcmp(cur_cpu_spec->cpu_name, "440GX Rev. F"))
		mtspr(SPRN_CCR1, mfspr(SPRN_CCR1) | CCR1_DPC);

	ibm44x_platform_init(r3, r4, r5, r6, r7);
}
