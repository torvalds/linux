/* SPDX-License-Identifier: GPL-2.0 */

#define FDOMAIN_REGION_SIZE	0x10
#define FDOMAIN_BIOS_SIZE	0x2000

enum {
	in_arbitration	= 0x02,
	in_selection	= 0x04,
	in_other	= 0x08,
	disconnect	= 0x10,
	aborted		= 0x20,
	sent_ident	= 0x40,
};

/* (@) = not present on TMC1800, (#) = not present on TMC1800 and TMC18C50 */
#define REG_SCSI_DATA		0	/* R/W: SCSI Data (with ACK) */
#define REG_BSTAT		1	/* R: SCSI Bus Status */
#define		BSTAT_BSY	BIT(0)	 /* Busy */
#define		BSTAT_MSG	BIT(1)	 /* Message */
#define		BSTAT_IO	BIT(2)	 /* Input/Output */
#define		BSTAT_CMD	BIT(3)	 /* Command/Data */
#define		BSTAT_REQ	BIT(4)	 /* Request and Not Ack */
#define		BSTAT_SEL	BIT(5)	 /* Select */
#define		BSTAT_ACK	BIT(6)	 /* Acknowledge and Request */
#define		BSTAT_ATN	BIT(7)	 /* Attention */
#define REG_BCTL		1	/* W: SCSI Bus Control */
#define		BCTL_RST	BIT(0)	 /* Bus Reset */
#define		BCTL_SEL	BIT(1)	 /* Select */
#define		BCTL_BSY	BIT(2)	 /* Busy */
#define		BCTL_ATN	BIT(3)	 /* Attention */
#define		BCTL_IO		BIT(4)	 /* Input/Output */
#define		BCTL_CMD	BIT(5)	 /* Command/Data */
#define		BCTL_MSG	BIT(6)	 /* Message */
#define		BCTL_BUSEN	BIT(7)	 /* Enable bus drivers */
#define REG_ASTAT		2	/* R: Adapter Status 1 */
#define		ASTAT_IRQ	BIT(0)	 /* Interrupt active */
#define		ASTAT_ARB	BIT(1)	 /* Arbitration complete */
#define		ASTAT_PARERR	BIT(2)	 /* Parity error */
#define		ASTAT_RST	BIT(3)	 /* SCSI reset occurred */
#define		ASTAT_FIFODIR	BIT(4)	 /* FIFO direction */
#define		ASTAT_FIFOEN	BIT(5)	 /* FIFO enabled */
#define		ASTAT_PAREN	BIT(6)	 /* Parity enabled */
#define		ASTAT_BUSEN	BIT(7)	 /* Bus drivers enabled */
#define REG_ICTL		2	/* W: Interrupt Control */
#define		ICTL_FIFO_MASK	0x0f	 /* FIFO threshold, 1/16 FIFO size */
#define		ICTL_FIFO	BIT(4)	 /* Int. on FIFO count */
#define		ICTL_ARB	BIT(5)	 /* Int. on Arbitration complete */
#define		ICTL_SEL	BIT(6)	 /* Int. on SCSI Select */
#define		ICTL_REQ	BIT(7)	 /* Int. on SCSI Request */
#define REG_FSTAT		3	/* R: Adapter Status 2 (FIFO) - (@) */
#define		FSTAT_ONOTEMPTY	BIT(0)	 /* Output FIFO not empty */
#define		FSTAT_INOTEMPTY	BIT(1)	 /* Input FIFO not empty */
#define		FSTAT_NOTEMPTY	BIT(2)	 /* Main FIFO not empty */
#define		FSTAT_NOTFULL	BIT(3)	 /* Main FIFO not full */
#define REG_MCTL		3	/* W: SCSI Data Mode Control */
#define		MCTL_ACK_MASK	0x0f	 /* Acknowledge period */
#define		MCTL_ACTDEASS	BIT(4)	 /* Active deassert of REQ and ACK */
#define		MCTL_TARGET	BIT(5)	 /* Enable target mode */
#define		MCTL_FASTSYNC	BIT(6)	 /* Enable Fast Synchronous */
#define		MCTL_SYNC	BIT(7)	 /* Enable Synchronous */
#define REG_INTCOND		4	/* R: Interrupt Condition - (@) */
#define		IRQ_FIFO	BIT(1)	 /* FIFO interrupt */
#define		IRQ_REQ		BIT(2)	 /* SCSI Request interrupt */
#define		IRQ_SEL		BIT(3)	 /* SCSI Select interrupt */
#define		IRQ_ARB		BIT(4)	 /* SCSI Arbitration interrupt */
#define		IRQ_RST		BIT(5)	 /* SCSI Reset interrupt */
#define		IRQ_FORCED	BIT(6)	 /* Forced interrupt */
#define		IRQ_TIMEOUT	BIT(7)	 /* Bus timeout */
#define REG_ACTL		4	/* W: Adapter Control 1 */
#define		ACTL_RESET	BIT(0)	 /* Reset FIFO, parity, reset int. */
#define		ACTL_FIRQ	BIT(1)	 /* Set Forced interrupt */
#define		ACTL_ARB	BIT(2)	 /* Initiate Bus Arbitration */
#define		ACTL_PAREN	BIT(3)	 /* Enable SCSI Parity */
#define		ACTL_IRQEN	BIT(4)	 /* Enable interrupts */
#define		ACTL_CLRFIRQ	BIT(5)	 /* Clear Forced interrupt */
#define		ACTL_FIFOWR	BIT(6)	 /* FIFO Direction (1=write) */
#define		ACTL_FIFOEN	BIT(7)	 /* Enable FIFO */
#define REG_ID_LSB		5	/* R: ID Code (LSB) */
#define REG_ACTL2		5	/* Adapter Control 2 - (@) */
#define		ACTL2_RAMOVRLY	BIT(0)	 /* Enable RAM overlay */
#define		ACTL2_SLEEP	BIT(7)	 /* Sleep mode */
#define REG_ID_MSB		6	/* R: ID Code (MSB) */
#define REG_LOOPBACK		7	/* R/W: Loopback */
#define REG_SCSI_DATA_NOACK	8	/* R/W: SCSI Data (no ACK) */
#define REG_ASTAT3		9	/* R: Adapter Status 3 */
#define		ASTAT3_ACTDEASS	BIT(0)	 /* Active deassert enabled */
#define		ASTAT3_RAMOVRLY	BIT(1)	 /* RAM overlay enabled */
#define		ASTAT3_TARGERR	BIT(2)	 /* Target error */
#define		ASTAT3_IRQEN	BIT(3)	 /* Interrupts enabled */
#define		ASTAT3_IRQMASK	0xf0	 /* Enabled interrupts mask */
#define REG_CFG1		10	/* R: Configuration Register 1 */
#define		CFG1_BUS	BIT(0)	 /* 0 = ISA */
#define		CFG1_IRQ_MASK	0x0e	 /* IRQ jumpers */
#define		CFG1_IO_MASK	0x30	 /* I/O base jumpers */
#define		CFG1_BIOS_MASK	0xc0	 /* BIOS base jumpers */
#define REG_CFG2		11	/* R/W: Configuration Register 2 (@) */
#define		CFG2_ROMDIS	BIT(0)	 /* ROM disabled */
#define		CFG2_RAMDIS	BIT(1)	 /* RAM disabled */
#define		CFG2_IRQEDGE	BIT(2)	 /* Edge-triggered interrupts */
#define		CFG2_NOWS	BIT(3)	 /* No wait states */
#define		CFG2_32BIT	BIT(7)	 /* 32-bit mode */
#define REG_FIFO		12	/* R/W: FIFO */
#define REG_FIFO_COUNT		14	/* R: FIFO Data Count */

#ifdef CONFIG_PM_SLEEP
static const struct dev_pm_ops fdomain_pm_ops;
#define FDOMAIN_PM_OPS	(&fdomain_pm_ops)
#else
#define FDOMAIN_PM_OPS	NULL
#endif /* CONFIG_PM_SLEEP */

struct Scsi_Host *fdomain_create(int base, int irq, int this_id,
				 struct device *dev);
int fdomain_destroy(struct Scsi_Host *sh);
