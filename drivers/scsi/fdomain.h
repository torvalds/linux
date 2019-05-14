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

enum in_port_type {
	Read_SCSI_Data	 =  0,
	SCSI_Status	 =  1,
	TMC_Status	 =  2,
	FIFO_Status	 =  3,	/* tmc18c50/tmc18c30 only */
	Interrupt_Cond	 =  4,	/* tmc18c50/tmc18c30 only */
	LSB_ID_Code	 =  5,
	MSB_ID_Code	 =  6,
	Read_Loopback	 =  7,
	SCSI_Data_NoACK	 =  8,
	Interrupt_Status =  9,
	Configuration1	 = 10,
	Configuration2	 = 11,	/* tmc18c50/tmc18c30 only */
	Read_FIFO	 = 12,
	FIFO_Data_Count	 = 14
};

enum out_port_type {
	Write_SCSI_Data	=  0,
	SCSI_Cntl	=  1,
	Interrupt_Cntl	=  2,
	SCSI_Mode_Cntl	=  3,
	TMC_Cntl	=  4,
	Memory_Cntl	=  5,	/* tmc18c50/tmc18c30 only */
	Write_Loopback	=  7,
	IO_Control	= 11,	/* tmc18c30 only */
	Write_FIFO	= 12
};

#ifdef CONFIG_PM_SLEEP
static const struct dev_pm_ops fdomain_pm_ops;
#define FDOMAIN_PM_OPS	(&fdomain_pm_ops)
#else
#define FDOMAIN_PM_OPS	NULL
#endif /* CONFIG_PM_SLEEP */

struct Scsi_Host *fdomain_create(int base, int irq, int this_id,
				 struct device *dev);
int fdomain_destroy(struct Scsi_Host *sh);
