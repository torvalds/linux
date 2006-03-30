/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2005 QLogic Corporation
 *
 * See LICENSE.qla2xxx for copyright and licensing details.
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
	ha->isp_ops.intr_handler(0, ha, NULL);
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

static inline uint8_t *host_to_fcp_swap(uint8_t *, uint32_t);
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

static inline int qla2x00_is_reserved_id(scsi_qla_host_t *, uint16_t);
static inline int
qla2x00_is_reserved_id(scsi_qla_host_t *ha, uint16_t loop_id)
{
	if (IS_QLA24XX(ha) || IS_QLA54XX(ha))
		return (loop_id > NPH_LAST_HANDLE);

	return ((loop_id > ha->last_loop_id && loop_id < SNS_FIRST_LOOP_ID) ||
	    loop_id == MANAGEMENT_SERVER || loop_id == BROADCAST);
};
