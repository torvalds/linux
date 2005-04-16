/*
 *                  QLOGIC LINUX SOFTWARE
 *
 * QLogic ISP2x00 device driver for Linux 2.6.x
 * Copyright (C) 2003-2004 QLogic Corporation
 * (www.qlogic.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 */


static __inline__ uint16_t qla2x00_debounce_register(volatile uint16_t __iomem *);
/*
 * qla2x00_debounce_register
 *      Debounce register.
 *
 * Input:
 *      port = register address.
 *
 * Returns:
 *      register value.
 */
static __inline__ uint16_t
qla2x00_debounce_register(volatile uint16_t __iomem *addr) 
{
	volatile uint16_t first;
	volatile uint16_t second;

	do {
		first = RD_REG_WORD(addr);
		barrier();
		cpu_relax();
		second = RD_REG_WORD(addr);
	} while (first != second);

	return (first);
}

static __inline__ int qla2x00_normalize_dma_addr(
    dma_addr_t *e_addr,  uint32_t *e_len,
    dma_addr_t *ne_addr, uint32_t *ne_len);

/**
 * qla2x00_normalize_dma_addr() - Normalize an DMA address.
 * @e_addr: Raw DMA address
 * @e_len: Raw DMA length
 * @ne_addr: Normalized second DMA address
 * @ne_len: Normalized second DMA length
 *
 * If the address does not span a 4GB page boundary, the contents of @ne_addr
 * and @ne_len are undefined.  @e_len is updated to reflect a normalization.
 *
 * Example:
 *
 * 	ffffabc0ffffeeee	(e_addr) start of DMA address
 * 	0000000020000000	(e_len)  length of DMA transfer
 *	ffffabc11fffeeed	end of DMA transfer
 *
 * Is the 4GB boundary crossed?
 *
 * 	ffffabc0ffffeeee	(e_addr)
 *	ffffabc11fffeeed	(e_addr + e_len - 1)
 *	00000001e0000003	((e_addr ^ (e_addr + e_len - 1))
 *	0000000100000000	((e_addr ^ (e_addr + e_len - 1)) & ~(0xffffffff)
 *
 * Compute start of second DMA segment:
 *
 * 	ffffabc0ffffeeee	(e_addr)
 *	ffffabc1ffffeeee	(0x100000000 + e_addr)
 *	ffffabc100000000	(0x100000000 + e_addr) & ~(0xffffffff)
 *	ffffabc100000000	(ne_addr)
 *	
 * Compute length of second DMA segment:
 *
 *	00000000ffffeeee	(e_addr & 0xffffffff)
 *	0000000000001112	(0x100000000 - (e_addr & 0xffffffff))
 *	000000001fffeeee	(e_len - (0x100000000 - (e_addr & 0xffffffff))
 *	000000001fffeeee	(ne_len)
 *
 * Adjust length of first DMA segment
 *
 * 	0000000020000000	(e_len)
 *	0000000000001112	(e_len - ne_len)
 *	0000000000001112	(e_len)
 *
 * Returns non-zero if the specified address was normalized, else zero.
 */
static __inline__ int
qla2x00_normalize_dma_addr(
    dma_addr_t *e_addr,  uint32_t *e_len,
    dma_addr_t *ne_addr, uint32_t *ne_len)
{
	int normalized;

	normalized = 0;
	if ((*e_addr ^ (*e_addr + *e_len - 1)) & ~(0xFFFFFFFFULL)) {
		/* Compute normalized crossed address and len */
		*ne_addr = (0x100000000ULL + *e_addr) & ~(0xFFFFFFFFULL);
		*ne_len = *e_len - (0x100000000ULL - (*e_addr & 0xFFFFFFFFULL));
		*e_len -= *ne_len;

		normalized++;
	}
	return (normalized);
}

static __inline__ void qla2x00_poll(scsi_qla_host_t *);
static inline void 
qla2x00_poll(scsi_qla_host_t *ha)
{
	if (IS_QLA2100(ha) || IS_QLA2200(ha))
		qla2100_intr_handler(0, ha, NULL);
	else
		qla2300_intr_handler(0, ha, NULL);
}


static __inline__ void qla2x00_enable_intrs(scsi_qla_host_t *);
static __inline__ void qla2x00_disable_intrs(scsi_qla_host_t *);

static inline void 
qla2x00_enable_intrs(scsi_qla_host_t *ha)
{
	unsigned long flags = 0;
	device_reg_t __iomem *reg = ha->iobase;

	spin_lock_irqsave(&ha->hardware_lock, flags);
	ha->interrupts_on = 1;
	/* enable risc and host interrupts */
	WRT_REG_WORD(&reg->ictrl, ICR_EN_INT | ICR_EN_RISC);
	RD_REG_WORD(&reg->ictrl);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

}

static inline void 
qla2x00_disable_intrs(scsi_qla_host_t *ha)
{
	unsigned long flags = 0;
	device_reg_t __iomem *reg = ha->iobase;

	spin_lock_irqsave(&ha->hardware_lock, flags);
	ha->interrupts_on = 0;
	/* disable risc and host interrupts */
	WRT_REG_WORD(&reg->ictrl, 0);
	RD_REG_WORD(&reg->ictrl);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
}


static __inline__ int qla2x00_is_wwn_zero(uint8_t *);

/*
 * qla2x00_is_wwn_zero - Check for zero node name
 *
 * Input:
 *      wwn = Pointer to WW name to check
 *
 * Returns:
 *      1 if name is 0x00 else 0
 *
 * Context:
 *      Kernel context.
 */
static __inline__ int
qla2x00_is_wwn_zero(uint8_t *wwn)
{
	int cnt;

	for (cnt = 0; cnt < WWN_SIZE ; cnt++, wwn++) {
		if (*wwn != 0)
			break;
	}
	/* if zero return 1 */
	if (cnt == WWN_SIZE)
		return (1);
	else
		return (0);
}

static __inline__ uint8_t
qla2x00_suspend_lun(scsi_qla_host_t *, os_lun_t *, int, int);
static __inline__ uint8_t
qla2x00_delay_lun(scsi_qla_host_t *, os_lun_t *, int);

static __inline__ uint8_t
qla2x00_suspend_lun(scsi_qla_host_t *ha, os_lun_t *lq, int time, int count)
{
	return (__qla2x00_suspend_lun(ha, lq, time, count, 0));
}

static __inline__ uint8_t
qla2x00_delay_lun(scsi_qla_host_t *ha, os_lun_t *lq, int time)
{
	return (__qla2x00_suspend_lun(ha, lq, time, 1, 1));
}

static __inline__ void qla2x00_check_fabric_devices(scsi_qla_host_t *);
/*
 * This routine will wait for fabric devices for
 * the reset delay.
 */
static __inline__ void qla2x00_check_fabric_devices(scsi_qla_host_t *ha) 
{
	uint16_t	fw_state;

	qla2x00_get_firmware_state(ha, &fw_state);
}

/**
 * qla2x00_issue_marker() - Issue a Marker IOCB if necessary.
 * @ha: HA context
 * @ha_locked: is function called with the hardware lock
 *
 * Returns non-zero if a failure occured, else zero.
 */
static inline int
qla2x00_issue_marker(scsi_qla_host_t *ha, int ha_locked)
{
	/* Send marker if required */
	if (ha->marker_needed != 0) {
		if (ha_locked) {
			if (__qla2x00_marker(ha, 0, 0, MK_SYNC_ALL) !=
			    QLA_SUCCESS)
				return (QLA_FUNCTION_FAILED);
		} else {
			if (qla2x00_marker(ha, 0, 0, MK_SYNC_ALL) !=
			    QLA_SUCCESS)
				return (QLA_FUNCTION_FAILED);
		}
		ha->marker_needed = 0;
	}
	return (QLA_SUCCESS);
}

static __inline__ void qla2x00_add_timer_to_cmd(srb_t *, int);
static __inline__ void qla2x00_delete_timer_from_cmd(srb_t *);

/**************************************************************************
*   qla2x00_add_timer_to_cmd
*
* Description:
*       Creates a timer for the specified command. The timeout is usually
*       the command time from kernel minus 2 secs.
*
* Input:
*     sp - pointer to validate
*
* Returns:
*     None.
**************************************************************************/
static inline void
qla2x00_add_timer_to_cmd(srb_t *sp, int timeout)
{
	init_timer(&sp->timer);
	sp->timer.expires = jiffies + timeout * HZ;
	sp->timer.data = (unsigned long) sp;
	sp->timer.function = (void (*) (unsigned long))qla2x00_cmd_timeout;
	add_timer(&sp->timer);
}

/**************************************************************************
*   qla2x00_delete_timer_from_cmd
*
* Description:
*       Delete the timer for the specified command.
*
* Input:
*     sp - pointer to validate
*
* Returns:
*     None.
**************************************************************************/
static inline void 
qla2x00_delete_timer_from_cmd(srb_t *sp)
{
	if (sp->timer.function != NULL) {
		del_timer(&sp->timer);
		sp->timer.function =  NULL;
		sp->timer.data = (unsigned long) NULL;
	}
}

