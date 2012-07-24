#ifndef BCM63XX_REGS_H_
#define BCM63XX_REGS_H_

/*************************************************************************
 * _REG relative to RSET_PERF
 *************************************************************************/

/* Chip Identifier / Revision register */
#define PERF_REV_REG			0x0
#define REV_CHIPID_SHIFT		16
#define REV_CHIPID_MASK			(0xffff << REV_CHIPID_SHIFT)
#define REV_REVID_SHIFT			0
#define REV_REVID_MASK			(0xffff << REV_REVID_SHIFT)

/* Clock Control register */
#define PERF_CKCTL_REG			0x4

#define CKCTL_6328_PHYMIPS_EN		(1 << 0)
#define CKCTL_6328_ADSL_QPROC_EN	(1 << 1)
#define CKCTL_6328_ADSL_AFE_EN		(1 << 2)
#define CKCTL_6328_ADSL_EN		(1 << 3)
#define CKCTL_6328_MIPS_EN		(1 << 4)
#define CKCTL_6328_SAR_EN		(1 << 5)
#define CKCTL_6328_PCM_EN		(1 << 6)
#define CKCTL_6328_USBD_EN		(1 << 7)
#define CKCTL_6328_USBH_EN		(1 << 8)
#define CKCTL_6328_HSSPI_EN		(1 << 9)
#define CKCTL_6328_PCIE_EN		(1 << 10)
#define CKCTL_6328_ROBOSW_EN		(1 << 11)

#define CKCTL_6328_ALL_SAFE_EN		(CKCTL_6328_PHYMIPS_EN |	\
					CKCTL_6328_ADSL_QPROC_EN |	\
					CKCTL_6328_ADSL_AFE_EN |	\
					CKCTL_6328_ADSL_EN |		\
					CKCTL_6328_SAR_EN  |		\
					CKCTL_6328_PCM_EN  |		\
					CKCTL_6328_USBD_EN |		\
					CKCTL_6328_USBH_EN |		\
					CKCTL_6328_ROBOSW_EN |		\
					CKCTL_6328_PCIE_EN)

#define CKCTL_6338_ADSLPHY_EN		(1 << 0)
#define CKCTL_6338_MPI_EN		(1 << 1)
#define CKCTL_6338_DRAM_EN		(1 << 2)
#define CKCTL_6338_ENET_EN		(1 << 4)
#define CKCTL_6338_USBS_EN		(1 << 4)
#define CKCTL_6338_SAR_EN		(1 << 5)
#define CKCTL_6338_SPI_EN		(1 << 9)

#define CKCTL_6338_ALL_SAFE_EN		(CKCTL_6338_ADSLPHY_EN |	\
					CKCTL_6338_MPI_EN |		\
					CKCTL_6338_ENET_EN |		\
					CKCTL_6338_SAR_EN |		\
					CKCTL_6338_SPI_EN)

#define CKCTL_6345_CPU_EN		(1 << 0)
#define CKCTL_6345_BUS_EN		(1 << 1)
#define CKCTL_6345_EBI_EN		(1 << 2)
#define CKCTL_6345_UART_EN		(1 << 3)
#define CKCTL_6345_ADSLPHY_EN		(1 << 4)
#define CKCTL_6345_ENET_EN		(1 << 7)
#define CKCTL_6345_USBH_EN		(1 << 8)

#define CKCTL_6345_ALL_SAFE_EN		(CKCTL_6345_ENET_EN |	\
					CKCTL_6345_USBH_EN |	\
					CKCTL_6345_ADSLPHY_EN)

#define CKCTL_6348_ADSLPHY_EN		(1 << 0)
#define CKCTL_6348_MPI_EN		(1 << 1)
#define CKCTL_6348_SDRAM_EN		(1 << 2)
#define CKCTL_6348_M2M_EN		(1 << 3)
#define CKCTL_6348_ENET_EN		(1 << 4)
#define CKCTL_6348_SAR_EN		(1 << 5)
#define CKCTL_6348_USBS_EN		(1 << 6)
#define CKCTL_6348_USBH_EN		(1 << 8)
#define CKCTL_6348_SPI_EN		(1 << 9)

#define CKCTL_6348_ALL_SAFE_EN		(CKCTL_6348_ADSLPHY_EN |	\
					CKCTL_6348_M2M_EN |		\
					CKCTL_6348_ENET_EN |		\
					CKCTL_6348_SAR_EN |		\
					CKCTL_6348_USBS_EN |		\
					CKCTL_6348_USBH_EN |		\
					CKCTL_6348_SPI_EN)

#define CKCTL_6358_ENET_EN		(1 << 4)
#define CKCTL_6358_ADSLPHY_EN		(1 << 5)
#define CKCTL_6358_PCM_EN		(1 << 8)
#define CKCTL_6358_SPI_EN		(1 << 9)
#define CKCTL_6358_USBS_EN		(1 << 10)
#define CKCTL_6358_SAR_EN		(1 << 11)
#define CKCTL_6358_EMUSB_EN		(1 << 17)
#define CKCTL_6358_ENET0_EN		(1 << 18)
#define CKCTL_6358_ENET1_EN		(1 << 19)
#define CKCTL_6358_USBSU_EN		(1 << 20)
#define CKCTL_6358_EPHY_EN		(1 << 21)

#define CKCTL_6358_ALL_SAFE_EN		(CKCTL_6358_ENET_EN |		\
					CKCTL_6358_ADSLPHY_EN |		\
					CKCTL_6358_PCM_EN |		\
					CKCTL_6358_SPI_EN |		\
					CKCTL_6358_USBS_EN |		\
					CKCTL_6358_SAR_EN |		\
					CKCTL_6358_EMUSB_EN |		\
					CKCTL_6358_ENET0_EN |		\
					CKCTL_6358_ENET1_EN |		\
					CKCTL_6358_USBSU_EN |		\
					CKCTL_6358_EPHY_EN)

#define CKCTL_6368_VDSL_QPROC_EN	(1 << 2)
#define CKCTL_6368_VDSL_AFE_EN		(1 << 3)
#define CKCTL_6368_VDSL_BONDING_EN	(1 << 4)
#define CKCTL_6368_VDSL_EN		(1 << 5)
#define CKCTL_6368_PHYMIPS_EN		(1 << 6)
#define CKCTL_6368_SWPKT_USB_EN		(1 << 7)
#define CKCTL_6368_SWPKT_SAR_EN		(1 << 8)
#define CKCTL_6368_SPI_EN		(1 << 9)
#define CKCTL_6368_USBD_EN		(1 << 10)
#define CKCTL_6368_SAR_EN		(1 << 11)
#define CKCTL_6368_ROBOSW_EN		(1 << 12)
#define CKCTL_6368_UTOPIA_EN		(1 << 13)
#define CKCTL_6368_PCM_EN		(1 << 14)
#define CKCTL_6368_USBH_EN		(1 << 15)
#define CKCTL_6368_DISABLE_GLESS_EN	(1 << 16)
#define CKCTL_6368_NAND_EN		(1 << 17)
#define CKCTL_6368_IPSEC_EN		(1 << 18)

#define CKCTL_6368_ALL_SAFE_EN		(CKCTL_6368_SWPKT_USB_EN |	\
					CKCTL_6368_SWPKT_SAR_EN |	\
					CKCTL_6368_SPI_EN |		\
					CKCTL_6368_USBD_EN |		\
					CKCTL_6368_SAR_EN |		\
					CKCTL_6368_ROBOSW_EN |		\
					CKCTL_6368_UTOPIA_EN |		\
					CKCTL_6368_PCM_EN |		\
					CKCTL_6368_USBH_EN |		\
					CKCTL_6368_DISABLE_GLESS_EN |	\
					CKCTL_6368_NAND_EN |		\
					CKCTL_6368_IPSEC_EN)

/* System PLL Control register  */
#define PERF_SYS_PLL_CTL_REG		0x8
#define SYS_PLL_SOFT_RESET		0x1

/* Interrupt Mask register */
#define PERF_IRQMASK_6328_REG		0x20
#define PERF_IRQMASK_6338_REG		0xc
#define PERF_IRQMASK_6345_REG		0xc
#define PERF_IRQMASK_6348_REG		0xc
#define PERF_IRQMASK_6358_REG		0xc
#define PERF_IRQMASK_6368_REG		0x20

/* Interrupt Status register */
#define PERF_IRQSTAT_6328_REG		0x28
#define PERF_IRQSTAT_6338_REG		0x10
#define PERF_IRQSTAT_6345_REG		0x10
#define PERF_IRQSTAT_6348_REG		0x10
#define PERF_IRQSTAT_6358_REG		0x10
#define PERF_IRQSTAT_6368_REG		0x28

/* External Interrupt Configuration register */
#define PERF_EXTIRQ_CFG_REG_6328	0x18
#define PERF_EXTIRQ_CFG_REG_6338	0x14
#define PERF_EXTIRQ_CFG_REG_6348	0x14
#define PERF_EXTIRQ_CFG_REG_6358	0x14
#define PERF_EXTIRQ_CFG_REG_6368	0x18

#define PERF_EXTIRQ_CFG_REG2_6368	0x1c

/* for 6348 only */
#define EXTIRQ_CFG_SENSE_6348(x)	(1 << (x))
#define EXTIRQ_CFG_STAT_6348(x)		(1 << (x + 5))
#define EXTIRQ_CFG_CLEAR_6348(x)	(1 << (x + 10))
#define EXTIRQ_CFG_MASK_6348(x)		(1 << (x + 15))
#define EXTIRQ_CFG_BOTHEDGE_6348(x)	(1 << (x + 20))
#define EXTIRQ_CFG_LEVELSENSE_6348(x)	(1 << (x + 25))
#define EXTIRQ_CFG_CLEAR_ALL_6348	(0xf << 10)
#define EXTIRQ_CFG_MASK_ALL_6348	(0xf << 15)

/* for all others */
#define EXTIRQ_CFG_SENSE(x)		(1 << (x))
#define EXTIRQ_CFG_STAT(x)		(1 << (x + 4))
#define EXTIRQ_CFG_CLEAR(x)		(1 << (x + 8))
#define EXTIRQ_CFG_MASK(x)		(1 << (x + 12))
#define EXTIRQ_CFG_BOTHEDGE(x)		(1 << (x + 16))
#define EXTIRQ_CFG_LEVELSENSE(x)	(1 << (x + 20))
#define EXTIRQ_CFG_CLEAR_ALL		(0xf << 8)
#define EXTIRQ_CFG_MASK_ALL		(0xf << 12)

/* Soft Reset register */
#define PERF_SOFTRESET_REG		0x28
#define PERF_SOFTRESET_6328_REG		0x10
#define PERF_SOFTRESET_6368_REG		0x10

#define SOFTRESET_6328_SPI_MASK		(1 << 0)
#define SOFTRESET_6328_EPHY_MASK	(1 << 1)
#define SOFTRESET_6328_SAR_MASK		(1 << 2)
#define SOFTRESET_6328_ENETSW_MASK	(1 << 3)
#define SOFTRESET_6328_USBS_MASK	(1 << 4)
#define SOFTRESET_6328_USBH_MASK	(1 << 5)
#define SOFTRESET_6328_PCM_MASK		(1 << 6)
#define SOFTRESET_6328_PCIE_CORE_MASK	(1 << 7)
#define SOFTRESET_6328_PCIE_MASK	(1 << 8)
#define SOFTRESET_6328_PCIE_EXT_MASK	(1 << 9)
#define SOFTRESET_6328_PCIE_HARD_MASK	(1 << 10)

#define SOFTRESET_6338_SPI_MASK		(1 << 0)
#define SOFTRESET_6338_ENET_MASK	(1 << 2)
#define SOFTRESET_6338_USBH_MASK	(1 << 3)
#define SOFTRESET_6338_USBS_MASK	(1 << 4)
#define SOFTRESET_6338_ADSL_MASK	(1 << 5)
#define SOFTRESET_6338_DMAMEM_MASK	(1 << 6)
#define SOFTRESET_6338_SAR_MASK		(1 << 7)
#define SOFTRESET_6338_ACLC_MASK	(1 << 8)
#define SOFTRESET_6338_ADSLMIPSPLL_MASK	(1 << 10)
#define SOFTRESET_6338_ALL	 (SOFTRESET_6338_SPI_MASK |		\
				  SOFTRESET_6338_ENET_MASK |		\
				  SOFTRESET_6338_USBH_MASK |		\
				  SOFTRESET_6338_USBS_MASK |		\
				  SOFTRESET_6338_ADSL_MASK |		\
				  SOFTRESET_6338_DMAMEM_MASK |		\
				  SOFTRESET_6338_SAR_MASK |		\
				  SOFTRESET_6338_ACLC_MASK |		\
				  SOFTRESET_6338_ADSLMIPSPLL_MASK)

#define SOFTRESET_6348_SPI_MASK		(1 << 0)
#define SOFTRESET_6348_ENET_MASK	(1 << 2)
#define SOFTRESET_6348_USBH_MASK	(1 << 3)
#define SOFTRESET_6348_USBS_MASK	(1 << 4)
#define SOFTRESET_6348_ADSL_MASK	(1 << 5)
#define SOFTRESET_6348_DMAMEM_MASK	(1 << 6)
#define SOFTRESET_6348_SAR_MASK		(1 << 7)
#define SOFTRESET_6348_ACLC_MASK	(1 << 8)
#define SOFTRESET_6348_ADSLMIPSPLL_MASK	(1 << 10)

#define SOFTRESET_6348_ALL	 (SOFTRESET_6348_SPI_MASK |		\
				  SOFTRESET_6348_ENET_MASK |		\
				  SOFTRESET_6348_USBH_MASK |		\
				  SOFTRESET_6348_USBS_MASK |		\
				  SOFTRESET_6348_ADSL_MASK |		\
				  SOFTRESET_6348_DMAMEM_MASK |		\
				  SOFTRESET_6348_SAR_MASK |		\
				  SOFTRESET_6348_ACLC_MASK |		\
				  SOFTRESET_6348_ADSLMIPSPLL_MASK)

#define SOFTRESET_6368_SPI_MASK		(1 << 0)
#define SOFTRESET_6368_MPI_MASK		(1 << 3)
#define SOFTRESET_6368_EPHY_MASK	(1 << 6)
#define SOFTRESET_6368_SAR_MASK		(1 << 7)
#define SOFTRESET_6368_ENETSW_MASK	(1 << 10)
#define SOFTRESET_6368_USBS_MASK	(1 << 11)
#define SOFTRESET_6368_USBH_MASK	(1 << 12)
#define SOFTRESET_6368_PCM_MASK		(1 << 13)

/* MIPS PLL control register */
#define PERF_MIPSPLLCTL_REG		0x34
#define MIPSPLLCTL_N1_SHIFT		20
#define MIPSPLLCTL_N1_MASK		(0x7 << MIPSPLLCTL_N1_SHIFT)
#define MIPSPLLCTL_N2_SHIFT		15
#define MIPSPLLCTL_N2_MASK		(0x1f << MIPSPLLCTL_N2_SHIFT)
#define MIPSPLLCTL_M1REF_SHIFT		12
#define MIPSPLLCTL_M1REF_MASK		(0x7 << MIPSPLLCTL_M1REF_SHIFT)
#define MIPSPLLCTL_M2REF_SHIFT		9
#define MIPSPLLCTL_M2REF_MASK		(0x7 << MIPSPLLCTL_M2REF_SHIFT)
#define MIPSPLLCTL_M1CPU_SHIFT		6
#define MIPSPLLCTL_M1CPU_MASK		(0x7 << MIPSPLLCTL_M1CPU_SHIFT)
#define MIPSPLLCTL_M1BUS_SHIFT		3
#define MIPSPLLCTL_M1BUS_MASK		(0x7 << MIPSPLLCTL_M1BUS_SHIFT)
#define MIPSPLLCTL_M2BUS_SHIFT		0
#define MIPSPLLCTL_M2BUS_MASK		(0x7 << MIPSPLLCTL_M2BUS_SHIFT)

/* ADSL PHY PLL Control register */
#define PERF_ADSLPLLCTL_REG		0x38
#define ADSLPLLCTL_N1_SHIFT		20
#define ADSLPLLCTL_N1_MASK		(0x7 << ADSLPLLCTL_N1_SHIFT)
#define ADSLPLLCTL_N2_SHIFT		15
#define ADSLPLLCTL_N2_MASK		(0x1f << ADSLPLLCTL_N2_SHIFT)
#define ADSLPLLCTL_M1REF_SHIFT		12
#define ADSLPLLCTL_M1REF_MASK		(0x7 << ADSLPLLCTL_M1REF_SHIFT)
#define ADSLPLLCTL_M2REF_SHIFT		9
#define ADSLPLLCTL_M2REF_MASK		(0x7 << ADSLPLLCTL_M2REF_SHIFT)
#define ADSLPLLCTL_M1CPU_SHIFT		6
#define ADSLPLLCTL_M1CPU_MASK		(0x7 << ADSLPLLCTL_M1CPU_SHIFT)
#define ADSLPLLCTL_M1BUS_SHIFT		3
#define ADSLPLLCTL_M1BUS_MASK		(0x7 << ADSLPLLCTL_M1BUS_SHIFT)
#define ADSLPLLCTL_M2BUS_SHIFT		0
#define ADSLPLLCTL_M2BUS_MASK		(0x7 << ADSLPLLCTL_M2BUS_SHIFT)

#define ADSLPLLCTL_VAL(n1, n2, m1ref, m2ref, m1cpu, m1bus, m2bus)	\
				(((n1) << ADSLPLLCTL_N1_SHIFT) |	\
				((n2) << ADSLPLLCTL_N2_SHIFT) |		\
				((m1ref) << ADSLPLLCTL_M1REF_SHIFT) |	\
				((m2ref) << ADSLPLLCTL_M2REF_SHIFT) |	\
				((m1cpu) << ADSLPLLCTL_M1CPU_SHIFT) |	\
				((m1bus) << ADSLPLLCTL_M1BUS_SHIFT) |	\
				((m2bus) << ADSLPLLCTL_M2BUS_SHIFT))


/*************************************************************************
 * _REG relative to RSET_TIMER
 *************************************************************************/

#define BCM63XX_TIMER_COUNT		4
#define TIMER_T0_ID			0
#define TIMER_T1_ID			1
#define TIMER_T2_ID			2
#define TIMER_WDT_ID			3

/* Timer irqstat register */
#define TIMER_IRQSTAT_REG		0
#define TIMER_IRQSTAT_TIMER_CAUSE(x)	(1 << (x))
#define TIMER_IRQSTAT_TIMER0_CAUSE	(1 << 0)
#define TIMER_IRQSTAT_TIMER1_CAUSE	(1 << 1)
#define TIMER_IRQSTAT_TIMER2_CAUSE	(1 << 2)
#define TIMER_IRQSTAT_WDT_CAUSE		(1 << 3)
#define TIMER_IRQSTAT_TIMER_IR_EN(x)	(1 << ((x) + 8))
#define TIMER_IRQSTAT_TIMER0_IR_EN	(1 << 8)
#define TIMER_IRQSTAT_TIMER1_IR_EN	(1 << 9)
#define TIMER_IRQSTAT_TIMER2_IR_EN	(1 << 10)

/* Timer control register */
#define TIMER_CTLx_REG(x)		(0x4 + (x * 4))
#define TIMER_CTL0_REG			0x4
#define TIMER_CTL1_REG			0x8
#define TIMER_CTL2_REG			0xC
#define TIMER_CTL_COUNTDOWN_MASK	(0x3fffffff)
#define TIMER_CTL_MONOTONIC_MASK	(1 << 30)
#define TIMER_CTL_ENABLE_MASK		(1 << 31)


/*************************************************************************
 * _REG relative to RSET_WDT
 *************************************************************************/

/* Watchdog default count register */
#define WDT_DEFVAL_REG			0x0

/* Watchdog control register */
#define WDT_CTL_REG			0x4

/* Watchdog control register constants */
#define WDT_START_1			(0xff00)
#define WDT_START_2			(0x00ff)
#define WDT_STOP_1			(0xee00)
#define WDT_STOP_2			(0x00ee)

/* Watchdog reset length register */
#define WDT_RSTLEN_REG			0x8

/* Watchdog soft reset register (BCM6328 only) */
#define WDT_SOFTRESET_REG		0xc

/*************************************************************************
 * _REG relative to RSET_UARTx
 *************************************************************************/

/* UART Control Register */
#define UART_CTL_REG			0x0
#define UART_CTL_RXTMOUTCNT_SHIFT	0
#define UART_CTL_RXTMOUTCNT_MASK	(0x1f << UART_CTL_RXTMOUTCNT_SHIFT)
#define UART_CTL_RSTTXDN_SHIFT		5
#define UART_CTL_RSTTXDN_MASK		(1 << UART_CTL_RSTTXDN_SHIFT)
#define UART_CTL_RSTRXFIFO_SHIFT		6
#define UART_CTL_RSTRXFIFO_MASK		(1 << UART_CTL_RSTRXFIFO_SHIFT)
#define UART_CTL_RSTTXFIFO_SHIFT		7
#define UART_CTL_RSTTXFIFO_MASK		(1 << UART_CTL_RSTTXFIFO_SHIFT)
#define UART_CTL_STOPBITS_SHIFT		8
#define UART_CTL_STOPBITS_MASK		(0xf << UART_CTL_STOPBITS_SHIFT)
#define UART_CTL_STOPBITS_1		(0x7 << UART_CTL_STOPBITS_SHIFT)
#define UART_CTL_STOPBITS_2		(0xf << UART_CTL_STOPBITS_SHIFT)
#define UART_CTL_BITSPERSYM_SHIFT	12
#define UART_CTL_BITSPERSYM_MASK	(0x3 << UART_CTL_BITSPERSYM_SHIFT)
#define UART_CTL_XMITBRK_SHIFT		14
#define UART_CTL_XMITBRK_MASK		(1 << UART_CTL_XMITBRK_SHIFT)
#define UART_CTL_RSVD_SHIFT		15
#define UART_CTL_RSVD_MASK		(1 << UART_CTL_RSVD_SHIFT)
#define UART_CTL_RXPAREVEN_SHIFT		16
#define UART_CTL_RXPAREVEN_MASK		(1 << UART_CTL_RXPAREVEN_SHIFT)
#define UART_CTL_RXPAREN_SHIFT		17
#define UART_CTL_RXPAREN_MASK		(1 << UART_CTL_RXPAREN_SHIFT)
#define UART_CTL_TXPAREVEN_SHIFT		18
#define UART_CTL_TXPAREVEN_MASK		(1 << UART_CTL_TXPAREVEN_SHIFT)
#define UART_CTL_TXPAREN_SHIFT		18
#define UART_CTL_TXPAREN_MASK		(1 << UART_CTL_TXPAREN_SHIFT)
#define UART_CTL_LOOPBACK_SHIFT		20
#define UART_CTL_LOOPBACK_MASK		(1 << UART_CTL_LOOPBACK_SHIFT)
#define UART_CTL_RXEN_SHIFT		21
#define UART_CTL_RXEN_MASK		(1 << UART_CTL_RXEN_SHIFT)
#define UART_CTL_TXEN_SHIFT		22
#define UART_CTL_TXEN_MASK		(1 << UART_CTL_TXEN_SHIFT)
#define UART_CTL_BRGEN_SHIFT		23
#define UART_CTL_BRGEN_MASK		(1 << UART_CTL_BRGEN_SHIFT)

/* UART Baudword register */
#define UART_BAUD_REG			0x4

/* UART Misc Control register */
#define UART_MCTL_REG			0x8
#define UART_MCTL_DTR_SHIFT		0
#define UART_MCTL_DTR_MASK		(1 << UART_MCTL_DTR_SHIFT)
#define UART_MCTL_RTS_SHIFT		1
#define UART_MCTL_RTS_MASK		(1 << UART_MCTL_RTS_SHIFT)
#define UART_MCTL_RXFIFOTHRESH_SHIFT	8
#define UART_MCTL_RXFIFOTHRESH_MASK	(0xf << UART_MCTL_RXFIFOTHRESH_SHIFT)
#define UART_MCTL_TXFIFOTHRESH_SHIFT	12
#define UART_MCTL_TXFIFOTHRESH_MASK	(0xf << UART_MCTL_TXFIFOTHRESH_SHIFT)
#define UART_MCTL_RXFIFOFILL_SHIFT	16
#define UART_MCTL_RXFIFOFILL_MASK	(0x1f << UART_MCTL_RXFIFOFILL_SHIFT)
#define UART_MCTL_TXFIFOFILL_SHIFT	24
#define UART_MCTL_TXFIFOFILL_MASK	(0x1f << UART_MCTL_TXFIFOFILL_SHIFT)

/* UART External Input Configuration register */
#define UART_EXTINP_REG			0xc
#define UART_EXTINP_RI_SHIFT		0
#define UART_EXTINP_RI_MASK		(1 << UART_EXTINP_RI_SHIFT)
#define UART_EXTINP_CTS_SHIFT		1
#define UART_EXTINP_CTS_MASK		(1 << UART_EXTINP_CTS_SHIFT)
#define UART_EXTINP_DCD_SHIFT		2
#define UART_EXTINP_DCD_MASK		(1 << UART_EXTINP_DCD_SHIFT)
#define UART_EXTINP_DSR_SHIFT		3
#define UART_EXTINP_DSR_MASK		(1 << UART_EXTINP_DSR_SHIFT)
#define UART_EXTINP_IRSTAT(x)		(1 << (x + 4))
#define UART_EXTINP_IRMASK(x)		(1 << (x + 8))
#define UART_EXTINP_IR_RI		0
#define UART_EXTINP_IR_CTS		1
#define UART_EXTINP_IR_DCD		2
#define UART_EXTINP_IR_DSR		3
#define UART_EXTINP_RI_NOSENSE_SHIFT	16
#define UART_EXTINP_RI_NOSENSE_MASK	(1 << UART_EXTINP_RI_NOSENSE_SHIFT)
#define UART_EXTINP_CTS_NOSENSE_SHIFT	17
#define UART_EXTINP_CTS_NOSENSE_MASK	(1 << UART_EXTINP_CTS_NOSENSE_SHIFT)
#define UART_EXTINP_DCD_NOSENSE_SHIFT	18
#define UART_EXTINP_DCD_NOSENSE_MASK	(1 << UART_EXTINP_DCD_NOSENSE_SHIFT)
#define UART_EXTINP_DSR_NOSENSE_SHIFT	19
#define UART_EXTINP_DSR_NOSENSE_MASK	(1 << UART_EXTINP_DSR_NOSENSE_SHIFT)

/* UART Interrupt register */
#define UART_IR_REG			0x10
#define UART_IR_MASK(x)			(1 << (x + 16))
#define UART_IR_STAT(x)			(1 << (x))
#define UART_IR_EXTIP			0
#define UART_IR_TXUNDER			1
#define UART_IR_TXOVER			2
#define UART_IR_TXTRESH			3
#define UART_IR_TXRDLATCH		4
#define UART_IR_TXEMPTY			5
#define UART_IR_RXUNDER			6
#define UART_IR_RXOVER			7
#define UART_IR_RXTIMEOUT		8
#define UART_IR_RXFULL			9
#define UART_IR_RXTHRESH		10
#define UART_IR_RXNOTEMPTY		11
#define UART_IR_RXFRAMEERR		12
#define UART_IR_RXPARERR		13
#define UART_IR_RXBRK			14
#define UART_IR_TXDONE			15

/* UART Fifo register */
#define UART_FIFO_REG			0x14
#define UART_FIFO_VALID_SHIFT		0
#define UART_FIFO_VALID_MASK		0xff
#define UART_FIFO_FRAMEERR_SHIFT	8
#define UART_FIFO_FRAMEERR_MASK		(1 << UART_FIFO_FRAMEERR_SHIFT)
#define UART_FIFO_PARERR_SHIFT		9
#define UART_FIFO_PARERR_MASK		(1 << UART_FIFO_PARERR_SHIFT)
#define UART_FIFO_BRKDET_SHIFT		10
#define UART_FIFO_BRKDET_MASK		(1 << UART_FIFO_BRKDET_SHIFT)
#define UART_FIFO_ANYERR_MASK		(UART_FIFO_FRAMEERR_MASK |	\
					UART_FIFO_PARERR_MASK |		\
					UART_FIFO_BRKDET_MASK)


/*************************************************************************
 * _REG relative to RSET_GPIO
 *************************************************************************/

/* GPIO registers */
#define GPIO_CTL_HI_REG			0x0
#define GPIO_CTL_LO_REG			0x4
#define GPIO_DATA_HI_REG		0x8
#define GPIO_DATA_LO_REG		0xC
#define GPIO_DATA_LO_REG_6345		0x8

/* GPIO mux registers and constants */
#define GPIO_MODE_REG			0x18

#define GPIO_MODE_6348_G4_DIAG		0x00090000
#define GPIO_MODE_6348_G4_UTOPIA	0x00080000
#define GPIO_MODE_6348_G4_LEGACY_LED	0x00030000
#define GPIO_MODE_6348_G4_MII_SNOOP	0x00020000
#define GPIO_MODE_6348_G4_EXT_EPHY	0x00010000
#define GPIO_MODE_6348_G3_DIAG		0x00009000
#define GPIO_MODE_6348_G3_UTOPIA	0x00008000
#define GPIO_MODE_6348_G3_EXT_MII	0x00007000
#define GPIO_MODE_6348_G2_DIAG		0x00000900
#define GPIO_MODE_6348_G2_PCI		0x00000500
#define GPIO_MODE_6348_G1_DIAG		0x00000090
#define GPIO_MODE_6348_G1_UTOPIA	0x00000080
#define GPIO_MODE_6348_G1_SPI_UART	0x00000060
#define GPIO_MODE_6348_G1_SPI_MASTER	0x00000060
#define GPIO_MODE_6348_G1_MII_PCCARD	0x00000040
#define GPIO_MODE_6348_G1_MII_SNOOP	0x00000020
#define GPIO_MODE_6348_G1_EXT_EPHY	0x00000010
#define GPIO_MODE_6348_G0_DIAG		0x00000009
#define GPIO_MODE_6348_G0_EXT_MII	0x00000007

#define GPIO_MODE_6358_EXTRACS		(1 << 5)
#define GPIO_MODE_6358_UART1		(1 << 6)
#define GPIO_MODE_6358_EXTRA_SPI_SS	(1 << 7)
#define GPIO_MODE_6358_SERIAL_LED	(1 << 10)
#define GPIO_MODE_6358_UTOPIA		(1 << 12)

#define GPIO_MODE_6368_ANALOG_AFE_0	(1 << 0)
#define GPIO_MODE_6368_ANALOG_AFE_1	(1 << 1)
#define GPIO_MODE_6368_SYS_IRQ		(1 << 2)
#define GPIO_MODE_6368_SERIAL_LED_DATA	(1 << 3)
#define GPIO_MODE_6368_SERIAL_LED_CLK	(1 << 4)
#define GPIO_MODE_6368_INET_LED		(1 << 5)
#define GPIO_MODE_6368_EPHY0_LED	(1 << 6)
#define GPIO_MODE_6368_EPHY1_LED	(1 << 7)
#define GPIO_MODE_6368_EPHY2_LED	(1 << 8)
#define GPIO_MODE_6368_EPHY3_LED	(1 << 9)
#define GPIO_MODE_6368_ROBOSW_LED_DAT	(1 << 10)
#define GPIO_MODE_6368_ROBOSW_LED_CLK	(1 << 11)
#define GPIO_MODE_6368_ROBOSW_LED0	(1 << 12)
#define GPIO_MODE_6368_ROBOSW_LED1	(1 << 13)
#define GPIO_MODE_6368_USBD_LED		(1 << 14)
#define GPIO_MODE_6368_NTR_PULSE	(1 << 15)
#define GPIO_MODE_6368_PCI_REQ1		(1 << 16)
#define GPIO_MODE_6368_PCI_GNT1		(1 << 17)
#define GPIO_MODE_6368_PCI_INTB		(1 << 18)
#define GPIO_MODE_6368_PCI_REQ0		(1 << 19)
#define GPIO_MODE_6368_PCI_GNT0		(1 << 20)
#define GPIO_MODE_6368_PCMCIA_CD1	(1 << 22)
#define GPIO_MODE_6368_PCMCIA_CD2	(1 << 23)
#define GPIO_MODE_6368_PCMCIA_VS1	(1 << 24)
#define GPIO_MODE_6368_PCMCIA_VS2	(1 << 25)
#define GPIO_MODE_6368_EBI_CS2		(1 << 26)
#define GPIO_MODE_6368_EBI_CS3		(1 << 27)
#define GPIO_MODE_6368_SPI_SSN2		(1 << 28)
#define GPIO_MODE_6368_SPI_SSN3		(1 << 29)
#define GPIO_MODE_6368_SPI_SSN4		(1 << 30)
#define GPIO_MODE_6368_SPI_SSN5		(1 << 31)


#define GPIO_BASEMODE_6368_REG		0x38
#define GPIO_BASEMODE_6368_UART2	0x1
#define GPIO_BASEMODE_6368_GPIO		0x0
#define GPIO_BASEMODE_6368_MASK		0x7
/* those bits must be kept as read in gpio basemode register*/

#define GPIO_STRAPBUS_REG		0x40
#define STRAPBUS_6358_BOOT_SEL_PARALLEL	(1 << 1)
#define STRAPBUS_6358_BOOT_SEL_SERIAL	(0 << 1)
#define STRAPBUS_6368_BOOT_SEL_MASK	0x3
#define STRAPBUS_6368_BOOT_SEL_NAND	0
#define STRAPBUS_6368_BOOT_SEL_SERIAL	1
#define STRAPBUS_6368_BOOT_SEL_PARALLEL	3


/*************************************************************************
 * _REG relative to RSET_ENET
 *************************************************************************/

/* Receiver Configuration register */
#define ENET_RXCFG_REG			0x0
#define ENET_RXCFG_ALLMCAST_SHIFT	1
#define ENET_RXCFG_ALLMCAST_MASK	(1 << ENET_RXCFG_ALLMCAST_SHIFT)
#define ENET_RXCFG_PROMISC_SHIFT	3
#define ENET_RXCFG_PROMISC_MASK		(1 << ENET_RXCFG_PROMISC_SHIFT)
#define ENET_RXCFG_LOOPBACK_SHIFT	4
#define ENET_RXCFG_LOOPBACK_MASK	(1 << ENET_RXCFG_LOOPBACK_SHIFT)
#define ENET_RXCFG_ENFLOW_SHIFT		5
#define ENET_RXCFG_ENFLOW_MASK		(1 << ENET_RXCFG_ENFLOW_SHIFT)

/* Receive Maximum Length register */
#define ENET_RXMAXLEN_REG		0x4
#define ENET_RXMAXLEN_SHIFT		0
#define ENET_RXMAXLEN_MASK		(0x7ff << ENET_RXMAXLEN_SHIFT)

/* Transmit Maximum Length register */
#define ENET_TXMAXLEN_REG		0x8
#define ENET_TXMAXLEN_SHIFT		0
#define ENET_TXMAXLEN_MASK		(0x7ff << ENET_TXMAXLEN_SHIFT)

/* MII Status/Control register */
#define ENET_MIISC_REG			0x10
#define ENET_MIISC_MDCFREQDIV_SHIFT	0
#define ENET_MIISC_MDCFREQDIV_MASK	(0x7f << ENET_MIISC_MDCFREQDIV_SHIFT)
#define ENET_MIISC_PREAMBLEEN_SHIFT	7
#define ENET_MIISC_PREAMBLEEN_MASK	(1 << ENET_MIISC_PREAMBLEEN_SHIFT)

/* MII Data register */
#define ENET_MIIDATA_REG		0x14
#define ENET_MIIDATA_DATA_SHIFT		0
#define ENET_MIIDATA_DATA_MASK		(0xffff << ENET_MIIDATA_DATA_SHIFT)
#define ENET_MIIDATA_TA_SHIFT		16
#define ENET_MIIDATA_TA_MASK		(0x3 << ENET_MIIDATA_TA_SHIFT)
#define ENET_MIIDATA_REG_SHIFT		18
#define ENET_MIIDATA_REG_MASK		(0x1f << ENET_MIIDATA_REG_SHIFT)
#define ENET_MIIDATA_PHYID_SHIFT	23
#define ENET_MIIDATA_PHYID_MASK		(0x1f << ENET_MIIDATA_PHYID_SHIFT)
#define ENET_MIIDATA_OP_READ_MASK	(0x6 << 28)
#define ENET_MIIDATA_OP_WRITE_MASK	(0x5 << 28)

/* Ethernet Interrupt Mask register */
#define ENET_IRMASK_REG			0x18

/* Ethernet Interrupt register */
#define ENET_IR_REG			0x1c
#define ENET_IR_MII			(1 << 0)
#define ENET_IR_MIB			(1 << 1)
#define ENET_IR_FLOWC			(1 << 2)

/* Ethernet Control register */
#define ENET_CTL_REG			0x2c
#define ENET_CTL_ENABLE_SHIFT		0
#define ENET_CTL_ENABLE_MASK		(1 << ENET_CTL_ENABLE_SHIFT)
#define ENET_CTL_DISABLE_SHIFT		1
#define ENET_CTL_DISABLE_MASK		(1 << ENET_CTL_DISABLE_SHIFT)
#define ENET_CTL_SRESET_SHIFT		2
#define ENET_CTL_SRESET_MASK		(1 << ENET_CTL_SRESET_SHIFT)
#define ENET_CTL_EPHYSEL_SHIFT		3
#define ENET_CTL_EPHYSEL_MASK		(1 << ENET_CTL_EPHYSEL_SHIFT)

/* Transmit Control register */
#define ENET_TXCTL_REG			0x30
#define ENET_TXCTL_FD_SHIFT		0
#define ENET_TXCTL_FD_MASK		(1 << ENET_TXCTL_FD_SHIFT)

/* Transmit Watermask register */
#define ENET_TXWMARK_REG		0x34
#define ENET_TXWMARK_WM_SHIFT		0
#define ENET_TXWMARK_WM_MASK		(0x3f << ENET_TXWMARK_WM_SHIFT)

/* MIB Control register */
#define ENET_MIBCTL_REG			0x38
#define ENET_MIBCTL_RDCLEAR_SHIFT	0
#define ENET_MIBCTL_RDCLEAR_MASK	(1 << ENET_MIBCTL_RDCLEAR_SHIFT)

/* Perfect Match Data Low register */
#define ENET_PML_REG(x)			(0x58 + (x) * 8)
#define ENET_PMH_REG(x)			(0x5c + (x) * 8)
#define ENET_PMH_DATAVALID_SHIFT	16
#define ENET_PMH_DATAVALID_MASK		(1 << ENET_PMH_DATAVALID_SHIFT)

/* MIB register */
#define ENET_MIB_REG(x)			(0x200 + (x) * 4)
#define ENET_MIB_REG_COUNT		55


/*************************************************************************
 * _REG relative to RSET_ENETDMA
 *************************************************************************/

/* Controller Configuration Register */
#define ENETDMA_CFG_REG			(0x0)
#define ENETDMA_CFG_EN_SHIFT		0
#define ENETDMA_CFG_EN_MASK		(1 << ENETDMA_CFG_EN_SHIFT)
#define ENETDMA_CFG_FLOWCH_MASK(x)	(1 << ((x >> 1) + 1))

/* Flow Control Descriptor Low Threshold register */
#define ENETDMA_FLOWCL_REG(x)		(0x4 + (x) * 6)

/* Flow Control Descriptor High Threshold register */
#define ENETDMA_FLOWCH_REG(x)		(0x8 + (x) * 6)

/* Flow Control Descriptor Buffer Alloca Threshold register */
#define ENETDMA_BUFALLOC_REG(x)		(0xc + (x) * 6)
#define ENETDMA_BUFALLOC_FORCE_SHIFT	31
#define ENETDMA_BUFALLOC_FORCE_MASK	(1 << ENETDMA_BUFALLOC_FORCE_SHIFT)

/* Channel Configuration register */
#define ENETDMA_CHANCFG_REG(x)		(0x100 + (x) * 0x10)
#define ENETDMA_CHANCFG_EN_SHIFT	0
#define ENETDMA_CHANCFG_EN_MASK		(1 << ENETDMA_CHANCFG_EN_SHIFT)
#define ENETDMA_CHANCFG_PKTHALT_SHIFT	1
#define ENETDMA_CHANCFG_PKTHALT_MASK	(1 << ENETDMA_CHANCFG_PKTHALT_SHIFT)

/* Interrupt Control/Status register */
#define ENETDMA_IR_REG(x)		(0x104 + (x) * 0x10)
#define ENETDMA_IR_BUFDONE_MASK		(1 << 0)
#define ENETDMA_IR_PKTDONE_MASK		(1 << 1)
#define ENETDMA_IR_NOTOWNER_MASK	(1 << 2)

/* Interrupt Mask register */
#define ENETDMA_IRMASK_REG(x)		(0x108 + (x) * 0x10)

/* Maximum Burst Length */
#define ENETDMA_MAXBURST_REG(x)		(0x10C + (x) * 0x10)

/* Ring Start Address register */
#define ENETDMA_RSTART_REG(x)		(0x200 + (x) * 0x10)

/* State Ram Word 2 */
#define ENETDMA_SRAM2_REG(x)		(0x204 + (x) * 0x10)

/* State Ram Word 3 */
#define ENETDMA_SRAM3_REG(x)		(0x208 + (x) * 0x10)

/* State Ram Word 4 */
#define ENETDMA_SRAM4_REG(x)		(0x20c + (x) * 0x10)


/*************************************************************************
 * _REG relative to RSET_ENETDMAC
 *************************************************************************/

/* Channel Configuration register */
#define ENETDMAC_CHANCFG_REG(x)		((x) * 0x10)
#define ENETDMAC_CHANCFG_EN_SHIFT	0
#define ENETDMAC_CHANCFG_EN_MASK	(1 << ENETDMA_CHANCFG_EN_SHIFT)
#define ENETDMAC_CHANCFG_PKTHALT_SHIFT	1
#define ENETDMAC_CHANCFG_PKTHALT_MASK	(1 << ENETDMA_CHANCFG_PKTHALT_SHIFT)

/* Interrupt Control/Status register */
#define ENETDMAC_IR_REG(x)		(0x4 + (x) * 0x10)
#define ENETDMAC_IR_BUFDONE_MASK	(1 << 0)
#define ENETDMAC_IR_PKTDONE_MASK	(1 << 1)
#define ENETDMAC_IR_NOTOWNER_MASK	(1 << 2)

/* Interrupt Mask register */
#define ENETDMAC_IRMASK_REG(x)		(0x8 + (x) * 0x10)

/* Maximum Burst Length */
#define ENETDMAC_MAXBURST_REG(x)	(0xc + (x) * 0x10)


/*************************************************************************
 * _REG relative to RSET_ENETDMAS
 *************************************************************************/

/* Ring Start Address register */
#define ENETDMAS_RSTART_REG(x)		((x) * 0x10)

/* State Ram Word 2 */
#define ENETDMAS_SRAM2_REG(x)		(0x4 + (x) * 0x10)

/* State Ram Word 3 */
#define ENETDMAS_SRAM3_REG(x)		(0x8 + (x) * 0x10)

/* State Ram Word 4 */
#define ENETDMAS_SRAM4_REG(x)		(0xc + (x) * 0x10)


/*************************************************************************
 * _REG relative to RSET_ENETSW
 *************************************************************************/

/* MIB register */
#define ENETSW_MIB_REG(x)		(0x2800 + (x) * 4)
#define ENETSW_MIB_REG_COUNT		47


/*************************************************************************
 * _REG relative to RSET_OHCI_PRIV
 *************************************************************************/

#define OHCI_PRIV_REG			0x0
#define OHCI_PRIV_PORT1_HOST_SHIFT	0
#define OHCI_PRIV_PORT1_HOST_MASK	(1 << OHCI_PRIV_PORT1_HOST_SHIFT)
#define OHCI_PRIV_REG_SWAP_SHIFT	3
#define OHCI_PRIV_REG_SWAP_MASK		(1 << OHCI_PRIV_REG_SWAP_SHIFT)


/*************************************************************************
 * _REG relative to RSET_USBH_PRIV
 *************************************************************************/

#define USBH_PRIV_SWAP_6358_REG		0x0
#define USBH_PRIV_SWAP_6368_REG		0x1c

#define USBH_PRIV_SWAP_EHCI_ENDN_SHIFT	4
#define USBH_PRIV_SWAP_EHCI_ENDN_MASK	(1 << USBH_PRIV_SWAP_EHCI_ENDN_SHIFT)
#define USBH_PRIV_SWAP_EHCI_DATA_SHIFT	3
#define USBH_PRIV_SWAP_EHCI_DATA_MASK	(1 << USBH_PRIV_SWAP_EHCI_DATA_SHIFT)
#define USBH_PRIV_SWAP_OHCI_ENDN_SHIFT	1
#define USBH_PRIV_SWAP_OHCI_ENDN_MASK	(1 << USBH_PRIV_SWAP_OHCI_ENDN_SHIFT)
#define USBH_PRIV_SWAP_OHCI_DATA_SHIFT	0
#define USBH_PRIV_SWAP_OHCI_DATA_MASK	(1 << USBH_PRIV_SWAP_OHCI_DATA_SHIFT)

#define USBH_PRIV_TEST_6358_REG		0x24
#define USBH_PRIV_TEST_6368_REG		0x14

#define USBH_PRIV_SETUP_6368_REG	0x28
#define USBH_PRIV_SETUP_IOC_SHIFT	4
#define USBH_PRIV_SETUP_IOC_MASK	(1 << USBH_PRIV_SETUP_IOC_SHIFT)



/*************************************************************************
 * _REG relative to RSET_MPI
 *************************************************************************/

/* well known (hard wired) chip select */
#define MPI_CS_PCMCIA_COMMON		4
#define MPI_CS_PCMCIA_ATTR		5
#define MPI_CS_PCMCIA_IO		6

/* Chip select base register */
#define MPI_CSBASE_REG(x)		(0x0 + (x) * 8)
#define MPI_CSBASE_BASE_SHIFT		13
#define MPI_CSBASE_BASE_MASK		(0x1ffff << MPI_CSBASE_BASE_SHIFT)
#define MPI_CSBASE_SIZE_SHIFT		0
#define MPI_CSBASE_SIZE_MASK		(0xf << MPI_CSBASE_SIZE_SHIFT)

#define MPI_CSBASE_SIZE_8K		0
#define MPI_CSBASE_SIZE_16K		1
#define MPI_CSBASE_SIZE_32K		2
#define MPI_CSBASE_SIZE_64K		3
#define MPI_CSBASE_SIZE_128K		4
#define MPI_CSBASE_SIZE_256K		5
#define MPI_CSBASE_SIZE_512K		6
#define MPI_CSBASE_SIZE_1M		7
#define MPI_CSBASE_SIZE_2M		8
#define MPI_CSBASE_SIZE_4M		9
#define MPI_CSBASE_SIZE_8M		10
#define MPI_CSBASE_SIZE_16M		11
#define MPI_CSBASE_SIZE_32M		12
#define MPI_CSBASE_SIZE_64M		13
#define MPI_CSBASE_SIZE_128M		14
#define MPI_CSBASE_SIZE_256M		15

/* Chip select control register */
#define MPI_CSCTL_REG(x)		(0x4 + (x) * 8)
#define MPI_CSCTL_ENABLE_MASK		(1 << 0)
#define MPI_CSCTL_WAIT_SHIFT		1
#define MPI_CSCTL_WAIT_MASK		(0x7 << MPI_CSCTL_WAIT_SHIFT)
#define MPI_CSCTL_DATA16_MASK		(1 << 4)
#define MPI_CSCTL_SYNCMODE_MASK		(1 << 7)
#define MPI_CSCTL_TSIZE_MASK		(1 << 8)
#define MPI_CSCTL_ENDIANSWAP_MASK	(1 << 10)
#define MPI_CSCTL_SETUP_SHIFT		16
#define MPI_CSCTL_SETUP_MASK		(0xf << MPI_CSCTL_SETUP_SHIFT)
#define MPI_CSCTL_HOLD_SHIFT		20
#define MPI_CSCTL_HOLD_MASK		(0xf << MPI_CSCTL_HOLD_SHIFT)

/* PCI registers */
#define MPI_SP0_RANGE_REG		0x100
#define MPI_SP0_REMAP_REG		0x104
#define MPI_SP0_REMAP_ENABLE_MASK	(1 << 0)
#define MPI_SP1_RANGE_REG		0x10C
#define MPI_SP1_REMAP_REG		0x110
#define MPI_SP1_REMAP_ENABLE_MASK	(1 << 0)

#define MPI_L2PCFG_REG			0x11C
#define MPI_L2PCFG_CFG_TYPE_SHIFT	0
#define MPI_L2PCFG_CFG_TYPE_MASK	(0x3 << MPI_L2PCFG_CFG_TYPE_SHIFT)
#define MPI_L2PCFG_REG_SHIFT		2
#define MPI_L2PCFG_REG_MASK		(0x3f << MPI_L2PCFG_REG_SHIFT)
#define MPI_L2PCFG_FUNC_SHIFT		8
#define MPI_L2PCFG_FUNC_MASK		(0x7 << MPI_L2PCFG_FUNC_SHIFT)
#define MPI_L2PCFG_DEVNUM_SHIFT		11
#define MPI_L2PCFG_DEVNUM_MASK		(0x1f << MPI_L2PCFG_DEVNUM_SHIFT)
#define MPI_L2PCFG_CFG_USEREG_MASK	(1 << 30)
#define MPI_L2PCFG_CFG_SEL_MASK		(1 << 31)

#define MPI_L2PMEMRANGE1_REG		0x120
#define MPI_L2PMEMBASE1_REG		0x124
#define MPI_L2PMEMREMAP1_REG		0x128
#define MPI_L2PMEMRANGE2_REG		0x12C
#define MPI_L2PMEMBASE2_REG		0x130
#define MPI_L2PMEMREMAP2_REG		0x134
#define MPI_L2PIORANGE_REG		0x138
#define MPI_L2PIOBASE_REG		0x13C
#define MPI_L2PIOREMAP_REG		0x140
#define MPI_L2P_BASE_MASK		(0xffff8000)
#define MPI_L2PREMAP_ENABLED_MASK	(1 << 0)
#define MPI_L2PREMAP_IS_CARDBUS_MASK	(1 << 2)

#define MPI_PCIMODESEL_REG		0x144
#define MPI_PCIMODESEL_BAR1_NOSWAP_MASK	(1 << 0)
#define MPI_PCIMODESEL_BAR2_NOSWAP_MASK	(1 << 1)
#define MPI_PCIMODESEL_EXT_ARB_MASK	(1 << 2)
#define MPI_PCIMODESEL_PREFETCH_SHIFT	4
#define MPI_PCIMODESEL_PREFETCH_MASK	(0xf << MPI_PCIMODESEL_PREFETCH_SHIFT)

#define MPI_LOCBUSCTL_REG		0x14C
#define MPI_LOCBUSCTL_EN_PCI_GPIO_MASK	(1 << 0)
#define MPI_LOCBUSCTL_U2P_NOSWAP_MASK	(1 << 1)

#define MPI_LOCINT_REG			0x150
#define MPI_LOCINT_MASK(x)		(1 << (x + 16))
#define MPI_LOCINT_STAT(x)		(1 << (x))
#define MPI_LOCINT_DIR_FAILED		6
#define MPI_LOCINT_EXT_PCI_INT		7
#define MPI_LOCINT_SERR			8
#define MPI_LOCINT_CSERR		9

#define MPI_PCICFGCTL_REG		0x178
#define MPI_PCICFGCTL_CFGADDR_SHIFT	2
#define MPI_PCICFGCTL_CFGADDR_MASK	(0x1f << MPI_PCICFGCTL_CFGADDR_SHIFT)
#define MPI_PCICFGCTL_WRITEEN_MASK	(1 << 7)

#define MPI_PCICFGDATA_REG		0x17C

/* PCI host bridge custom register */
#define BCMPCI_REG_TIMERS		0x40
#define REG_TIMER_TRDY_SHIFT		0
#define REG_TIMER_TRDY_MASK		(0xff << REG_TIMER_TRDY_SHIFT)
#define REG_TIMER_RETRY_SHIFT		8
#define REG_TIMER_RETRY_MASK		(0xff << REG_TIMER_RETRY_SHIFT)


/*************************************************************************
 * _REG relative to RSET_PCMCIA
 *************************************************************************/

#define PCMCIA_C1_REG			0x0
#define PCMCIA_C1_CD1_MASK		(1 << 0)
#define PCMCIA_C1_CD2_MASK		(1 << 1)
#define PCMCIA_C1_VS1_MASK		(1 << 2)
#define PCMCIA_C1_VS2_MASK		(1 << 3)
#define PCMCIA_C1_VS1OE_MASK		(1 << 6)
#define PCMCIA_C1_VS2OE_MASK		(1 << 7)
#define PCMCIA_C1_CBIDSEL_SHIFT		(8)
#define PCMCIA_C1_CBIDSEL_MASK		(0x1f << PCMCIA_C1_CBIDSEL_SHIFT)
#define PCMCIA_C1_EN_PCMCIA_GPIO_MASK	(1 << 13)
#define PCMCIA_C1_EN_PCMCIA_MASK	(1 << 14)
#define PCMCIA_C1_EN_CARDBUS_MASK	(1 << 15)
#define PCMCIA_C1_RESET_MASK		(1 << 18)

#define PCMCIA_C2_REG			0x8
#define PCMCIA_C2_DATA16_MASK		(1 << 0)
#define PCMCIA_C2_BYTESWAP_MASK		(1 << 1)
#define PCMCIA_C2_RWCOUNT_SHIFT		2
#define PCMCIA_C2_RWCOUNT_MASK		(0x3f << PCMCIA_C2_RWCOUNT_SHIFT)
#define PCMCIA_C2_INACTIVE_SHIFT	8
#define PCMCIA_C2_INACTIVE_MASK		(0x3f << PCMCIA_C2_INACTIVE_SHIFT)
#define PCMCIA_C2_SETUP_SHIFT		16
#define PCMCIA_C2_SETUP_MASK		(0x3f << PCMCIA_C2_SETUP_SHIFT)
#define PCMCIA_C2_HOLD_SHIFT		24
#define PCMCIA_C2_HOLD_MASK		(0x3f << PCMCIA_C2_HOLD_SHIFT)


/*************************************************************************
 * _REG relative to RSET_SDRAM
 *************************************************************************/

#define SDRAM_CFG_REG			0x0
#define SDRAM_CFG_ROW_SHIFT		4
#define SDRAM_CFG_ROW_MASK		(0x3 << SDRAM_CFG_ROW_SHIFT)
#define SDRAM_CFG_COL_SHIFT		6
#define SDRAM_CFG_COL_MASK		(0x3 << SDRAM_CFG_COL_SHIFT)
#define SDRAM_CFG_32B_SHIFT		10
#define SDRAM_CFG_32B_MASK		(1 << SDRAM_CFG_32B_SHIFT)
#define SDRAM_CFG_BANK_SHIFT		13
#define SDRAM_CFG_BANK_MASK		(1 << SDRAM_CFG_BANK_SHIFT)

#define SDRAM_MBASE_REG			0xc

#define SDRAM_PRIO_REG			0x2C
#define SDRAM_PRIO_MIPS_SHIFT		29
#define SDRAM_PRIO_MIPS_MASK		(1 << SDRAM_PRIO_MIPS_SHIFT)
#define SDRAM_PRIO_ADSL_SHIFT		30
#define SDRAM_PRIO_ADSL_MASK		(1 << SDRAM_PRIO_ADSL_SHIFT)
#define SDRAM_PRIO_EN_SHIFT		31
#define SDRAM_PRIO_EN_MASK		(1 << SDRAM_PRIO_EN_SHIFT)


/*************************************************************************
 * _REG relative to RSET_MEMC
 *************************************************************************/

#define MEMC_CFG_REG			0x4
#define MEMC_CFG_32B_SHIFT		1
#define MEMC_CFG_32B_MASK		(1 << MEMC_CFG_32B_SHIFT)
#define MEMC_CFG_COL_SHIFT		3
#define MEMC_CFG_COL_MASK		(0x3 << MEMC_CFG_COL_SHIFT)
#define MEMC_CFG_ROW_SHIFT		6
#define MEMC_CFG_ROW_MASK		(0x3 << MEMC_CFG_ROW_SHIFT)


/*************************************************************************
 * _REG relative to RSET_DDR
 *************************************************************************/

#define DDR_CSEND_REG			0x8

#define DDR_DMIPSPLLCFG_REG		0x18
#define DMIPSPLLCFG_M1_SHIFT		0
#define DMIPSPLLCFG_M1_MASK		(0xff << DMIPSPLLCFG_M1_SHIFT)
#define DMIPSPLLCFG_N1_SHIFT		23
#define DMIPSPLLCFG_N1_MASK		(0x3f << DMIPSPLLCFG_N1_SHIFT)
#define DMIPSPLLCFG_N2_SHIFT		29
#define DMIPSPLLCFG_N2_MASK		(0x7 << DMIPSPLLCFG_N2_SHIFT)

#define DDR_DMIPSPLLCFG_6368_REG	0x20
#define DMIPSPLLCFG_6368_P1_SHIFT	0
#define DMIPSPLLCFG_6368_P1_MASK	(0xf << DMIPSPLLCFG_6368_P1_SHIFT)
#define DMIPSPLLCFG_6368_P2_SHIFT	4
#define DMIPSPLLCFG_6368_P2_MASK	(0xf << DMIPSPLLCFG_6368_P2_SHIFT)
#define DMIPSPLLCFG_6368_NDIV_SHIFT	16
#define DMIPSPLLCFG_6368_NDIV_MASK	(0x1ff << DMIPSPLLCFG_6368_NDIV_SHIFT)

#define DDR_DMIPSPLLDIV_6368_REG	0x24
#define DMIPSPLLDIV_6368_MDIV_SHIFT	0
#define DMIPSPLLDIV_6368_MDIV_MASK	(0xff << DMIPSPLLDIV_6368_MDIV_SHIFT)


/*************************************************************************
 * _REG relative to RSET_M2M
 *************************************************************************/

#define M2M_RX				0
#define M2M_TX				1

#define M2M_SRC_REG(x)			((x) * 0x40 + 0x00)
#define M2M_DST_REG(x)			((x) * 0x40 + 0x04)
#define M2M_SIZE_REG(x)			((x) * 0x40 + 0x08)

#define M2M_CTRL_REG(x)			((x) * 0x40 + 0x0c)
#define M2M_CTRL_ENABLE_MASK		(1 << 0)
#define M2M_CTRL_IRQEN_MASK		(1 << 1)
#define M2M_CTRL_ERROR_CLR_MASK		(1 << 6)
#define M2M_CTRL_DONE_CLR_MASK		(1 << 7)
#define M2M_CTRL_NOINC_MASK		(1 << 8)
#define M2M_CTRL_PCMCIASWAP_MASK	(1 << 9)
#define M2M_CTRL_SWAPBYTE_MASK		(1 << 10)
#define M2M_CTRL_ENDIAN_MASK		(1 << 11)

#define M2M_STAT_REG(x)			((x) * 0x40 + 0x10)
#define M2M_STAT_DONE			(1 << 0)
#define M2M_STAT_ERROR			(1 << 1)

#define M2M_SRCID_REG(x)		((x) * 0x40 + 0x14)
#define M2M_DSTID_REG(x)		((x) * 0x40 + 0x18)

/*************************************************************************
 * _REG relative to RSET_RNG
 *************************************************************************/

#define RNG_CTRL			0x00
#define RNG_EN				(1 << 0)

#define RNG_STAT			0x04
#define RNG_AVAIL_MASK			(0xff000000)

#define RNG_DATA			0x08
#define RNG_THRES			0x0c
#define RNG_MASK			0x10

/*************************************************************************
 * _REG relative to RSET_SPI
 *************************************************************************/

/* BCM 6338 SPI core */
#define SPI_6338_CMD			0x00	/* 16-bits register */
#define SPI_6338_INT_STATUS		0x02
#define SPI_6338_INT_MASK_ST		0x03
#define SPI_6338_INT_MASK		0x04
#define SPI_6338_ST			0x05
#define SPI_6338_CLK_CFG		0x06
#define SPI_6338_FILL_BYTE		0x07
#define SPI_6338_MSG_TAIL		0x09
#define SPI_6338_RX_TAIL		0x0b
#define SPI_6338_MSG_CTL		0x40
#define SPI_6338_MSG_DATA		0x41
#define SPI_6338_MSG_DATA_SIZE		0x3f
#define SPI_6338_RX_DATA		0x80
#define SPI_6338_RX_DATA_SIZE		0x3f

/* BCM 6348 SPI core */
#define SPI_6348_CMD			0x00	/* 16-bits register */
#define SPI_6348_INT_STATUS		0x02
#define SPI_6348_INT_MASK_ST		0x03
#define SPI_6348_INT_MASK		0x04
#define SPI_6348_ST			0x05
#define SPI_6348_CLK_CFG		0x06
#define SPI_6348_FILL_BYTE		0x07
#define SPI_6348_MSG_TAIL		0x09
#define SPI_6348_RX_TAIL		0x0b
#define SPI_6348_MSG_CTL		0x40
#define SPI_6348_MSG_DATA		0x41
#define SPI_6348_MSG_DATA_SIZE		0x3f
#define SPI_6348_RX_DATA		0x80
#define SPI_6348_RX_DATA_SIZE		0x3f

/* BCM 6358 SPI core */
#define SPI_6358_MSG_CTL		0x00	/* 16-bits register */
#define SPI_6358_MSG_DATA		0x02
#define SPI_6358_MSG_DATA_SIZE		0x21e
#define SPI_6358_RX_DATA		0x400
#define SPI_6358_RX_DATA_SIZE		0x220
#define SPI_6358_CMD			0x700	/* 16-bits register */
#define SPI_6358_INT_STATUS		0x702
#define SPI_6358_INT_MASK_ST		0x703
#define SPI_6358_INT_MASK		0x704
#define SPI_6358_ST			0x705
#define SPI_6358_CLK_CFG		0x706
#define SPI_6358_FILL_BYTE		0x707
#define SPI_6358_MSG_TAIL		0x709
#define SPI_6358_RX_TAIL		0x70B

/* BCM 6358 SPI core */
#define SPI_6368_MSG_CTL		0x00	/* 16-bits register */
#define SPI_6368_MSG_DATA		0x02
#define SPI_6368_MSG_DATA_SIZE		0x21e
#define SPI_6368_RX_DATA		0x400
#define SPI_6368_RX_DATA_SIZE		0x220
#define SPI_6368_CMD			0x700	/* 16-bits register */
#define SPI_6368_INT_STATUS		0x702
#define SPI_6368_INT_MASK_ST		0x703
#define SPI_6368_INT_MASK		0x704
#define SPI_6368_ST			0x705
#define SPI_6368_CLK_CFG		0x706
#define SPI_6368_FILL_BYTE		0x707
#define SPI_6368_MSG_TAIL		0x709
#define SPI_6368_RX_TAIL		0x70B

/* Shared SPI definitions */

/* Message configuration */
#define SPI_FD_RW			0x00
#define SPI_HD_W			0x01
#define SPI_HD_R			0x02
#define SPI_BYTE_CNT_SHIFT		0
#define SPI_MSG_TYPE_SHIFT		14

/* Command */
#define SPI_CMD_NOOP			0x00
#define SPI_CMD_SOFT_RESET		0x01
#define SPI_CMD_HARD_RESET		0x02
#define SPI_CMD_START_IMMEDIATE		0x03
#define SPI_CMD_COMMAND_SHIFT		0
#define SPI_CMD_COMMAND_MASK		0x000f
#define SPI_CMD_DEVICE_ID_SHIFT		4
#define SPI_CMD_PREPEND_BYTE_CNT_SHIFT	8
#define SPI_CMD_ONE_BYTE_SHIFT		11
#define SPI_CMD_ONE_WIRE_SHIFT		12
#define SPI_DEV_ID_0			0
#define SPI_DEV_ID_1			1
#define SPI_DEV_ID_2			2
#define SPI_DEV_ID_3			3

/* Interrupt mask */
#define SPI_INTR_CMD_DONE		0x01
#define SPI_INTR_RX_OVERFLOW		0x02
#define SPI_INTR_TX_UNDERFLOW		0x04
#define SPI_INTR_TX_OVERFLOW		0x08
#define SPI_INTR_RX_UNDERFLOW		0x10
#define SPI_INTR_CLEAR_ALL		0x1f

/* Status */
#define SPI_RX_EMPTY			0x02
#define SPI_CMD_BUSY			0x04
#define SPI_SERIAL_BUSY			0x08

/* Clock configuration */
#define SPI_CLK_20MHZ			0x00
#define SPI_CLK_0_391MHZ		0x01
#define SPI_CLK_0_781MHZ		0x02 /* default */
#define SPI_CLK_1_563MHZ		0x03
#define SPI_CLK_3_125MHZ		0x04
#define SPI_CLK_6_250MHZ		0x05
#define SPI_CLK_12_50MHZ		0x06
#define SPI_CLK_MASK			0x07
#define SPI_SSOFFTIME_MASK		0x38
#define SPI_SSOFFTIME_SHIFT		3
#define SPI_BYTE_SWAP			0x80

/*************************************************************************
 * _REG relative to RSET_MISC
 *************************************************************************/

#define MISC_STRAPBUS_6328_REG		0x240
#define STRAPBUS_6328_FCVO_SHIFT	7
#define STRAPBUS_6328_FCVO_MASK		(0x1f << STRAPBUS_6328_FCVO_SHIFT)
#define STRAPBUS_6328_BOOT_SEL_SERIAL	(1 << 28)
#define STRAPBUS_6328_BOOT_SEL_NAND	(0 << 28)

#endif /* BCM63XX_REGS_H_ */
