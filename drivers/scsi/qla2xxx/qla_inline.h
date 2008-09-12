/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2008 QLogic Corporation
 *
 * See LICENSE.qla2xxx for copyright and licensing details.
 */

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

static inline void
qla2x00_poll(scsi_qla_host_t *ha)
{
	unsigned long flags;

	local_irq_save(flags);
	ha->isp_ops->intr_handler(0, ha);
	local_irq_restore(flags);
}

static __inline__ scsi_qla_host_t *
to_qla_parent(scsi_qla_host_t *ha)
{
	return ha->parent ? ha->parent : ha;
}

/**
 * qla2x00_issue_marker() - Issue a Marker IOCB if necessary.
 * @ha: HA context
 * @ha_locked: is function called with the hardware lock
 *
 * Returns non-zero if a failure occurred, else zero.
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

static inline uint8_t *
host_to_fcp_swap(uint8_t *fcp, uint32_t bsize)
{
       uint32_t *ifcp = (uint32_t *) fcp;
       uint32_t *ofcp = (uint32_t *) fcp;
       uint32_t iter = bsize >> 2;

       for (; iter ; iter--)
               *ofcp++ = swab32(*ifcp++);

       return fcp;
}

static inline int
qla2x00_is_reserved_id(scsi_qla_host_t *ha, uint16_t loop_id)
{
	if (IS_FWI2_CAPABLE(ha))
		return (loop_id > NPH_LAST_HANDLE);

	return ((loop_id > ha->last_loop_id && loop_id < SNS_FIRST_LOOP_ID) ||
	    loop_id == MANAGEMENT_SERVER || loop_id == BROADCAST);
};
