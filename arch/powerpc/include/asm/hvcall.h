#ifndef _ASM_POWERPC_HVCALL_H
#define _ASM_POWERPC_HVCALL_H
#ifdef __KERNEL__

#define HVSC			.long 0x44000022

#define H_SUCCESS	0
#define H_BUSY		1	/* Hardware busy -- retry later */
#define H_CLOSED	2	/* Resource closed */
#define H_NOT_AVAILABLE 3
#define H_CONSTRAINED	4	/* Resource request constrained to max allowed */
#define H_PARTIAL       5
#define H_IN_PROGRESS	14	/* Kind of like busy */
#define H_PAGE_REGISTERED 15
#define H_PARTIAL_STORE   16
#define H_PENDING	17	/* returned from H_POLL_PENDING */
#define H_CONTINUE	18	/* Returned from H_Join on success */
#define H_LONG_BUSY_START_RANGE		9900  /* Start of long busy range */
#define H_LONG_BUSY_ORDER_1_MSEC	9900  /* Long busy, hint that 1msec \
						 is a good time to retry */
#define H_LONG_BUSY_ORDER_10_MSEC	9901  /* Long busy, hint that 10msec \
						 is a good time to retry */
#define H_LONG_BUSY_ORDER_100_MSEC 	9902  /* Long busy, hint that 100msec \
						 is a good time to retry */
#define H_LONG_BUSY_ORDER_1_SEC		9903  /* Long busy, hint that 1sec \
						 is a good time to retry */
#define H_LONG_BUSY_ORDER_10_SEC	9904  /* Long busy, hint that 10sec \
						 is a good time to retry */
#define H_LONG_BUSY_ORDER_100_SEC	9905  /* Long busy, hint that 100sec \
						 is a good time to retry */
#define H_LONG_BUSY_END_RANGE		9905  /* End of long busy range */

/* Internal value used in book3s_hv kvm support; not returned to guests */
#define H_TOO_HARD	9999

#define H_HARDWARE	-1	/* Hardware error */
#define H_FUNCTION	-2	/* Function not supported */
#define H_PRIVILEGE	-3	/* Caller not privileged */
#define H_PARAMETER	-4	/* Parameter invalid, out-of-range or conflicting */
#define H_BAD_MODE	-5	/* Illegal msr value */
#define H_PTEG_FULL	-6	/* PTEG is full */
#define H_NOT_FOUND	-7	/* PTE was not found" */
#define H_RESERVED_DABR	-8	/* DABR address is reserved by the hypervisor on this processor" */
#define H_NO_MEM	-9
#define H_AUTHORITY	-10
#define H_PERMISSION	-11
#define H_DROPPED	-12
#define H_SOURCE_PARM	-13
#define H_DEST_PARM	-14
#define H_REMOTE_PARM	-15
#define H_RESOURCE	-16
#define H_ADAPTER_PARM  -17
#define H_RH_PARM       -18
#define H_RCQ_PARM      -19
#define H_SCQ_PARM      -20
#define H_EQ_PARM       -21
#define H_RT_PARM       -22
#define H_ST_PARM       -23
#define H_SIGT_PARM     -24
#define H_TOKEN_PARM    -25
#define H_MLENGTH_PARM  -27
#define H_MEM_PARM      -28
#define H_MEM_ACCESS_PARM -29
#define H_ATTR_PARM     -30
#define H_PORT_PARM     -31
#define H_MCG_PARM      -32
#define H_VL_PARM       -33
#define H_TSIZE_PARM    -34
#define H_TRACE_PARM    -35

#define H_MASK_PARM     -37
#define H_MCG_FULL      -38
#define H_ALIAS_EXIST   -39
#define H_P_COUNTER     -40
#define H_TABLE_FULL    -41
#define H_ALT_TABLE     -42
#define H_MR_CONDITION  -43
#define H_NOT_ENOUGH_RESOURCES -44
#define H_R_STATE       -45
#define H_RESCINDED     -46
#define H_P2		-55
#define H_P3		-56
#define H_P4		-57
#define H_P5		-58
#define H_P6		-59
#define H_P7		-60
#define H_P8		-61
#define H_P9		-62
#define H_TOO_BIG	-64
#define H_OVERLAP	-68
#define H_INTERRUPT	-69
#define H_BAD_DATA	-70
#define H_NOT_ACTIVE	-71
#define H_SG_LIST	-72
#define H_OP_MODE	-73
#define H_COP_HW	-74
#define H_STATE		-75
#define H_UNSUPPORTED_FLAG_START	-256
#define H_UNSUPPORTED_FLAG_END		-511
#define H_MULTI_THREADS_ACTIVE	-9005
#define H_OUTSTANDING_COP_OPS	-9006


/* Long Busy is a condition that can be returned by the firmware
 * when a call cannot be completed now, but the identical call
 * should be retried later.  This prevents calls blocking in the
 * firmware for long periods of time.  Annoyingly the firmware can return
 * a range of return codes, hinting at how long we should wait before
 * retrying.  If you don't care for the hint, the macro below is a good
 * way to check for the long_busy return codes
 */
#define H_IS_LONG_BUSY(x)  ((x >= H_LONG_BUSY_START_RANGE) \
			     && (x <= H_LONG_BUSY_END_RANGE))

/* Flags */
#define H_LARGE_PAGE		(1UL<<(63-16))
#define H_EXACT			(1UL<<(63-24))	/* Use exact PTE or return H_PTEG_FULL */
#define H_R_XLATE		(1UL<<(63-25))	/* include a valid logical page num in the pte if the valid bit is set */
#define H_READ_4		(1UL<<(63-26))	/* Return 4 PTEs */
#define H_PAGE_STATE_CHANGE	(1UL<<(63-28))
#define H_PAGE_UNUSED		((1UL<<(63-29)) | (1UL<<(63-30)))
#define H_PAGE_SET_UNUSED	(H_PAGE_STATE_CHANGE | H_PAGE_UNUSED)
#define H_PAGE_SET_LOANED	(H_PAGE_SET_UNUSED | (1UL<<(63-31)))
#define H_PAGE_SET_ACTIVE	H_PAGE_STATE_CHANGE
#define H_AVPN			(1UL<<(63-32))	/* An avpn is provided as a sanity test */
#define H_ANDCOND		(1UL<<(63-33))
#define H_LOCAL			(1UL<<(63-35))
#define H_ICACHE_INVALIDATE	(1UL<<(63-40))	/* icbi, etc.  (ignored for IO pages) */
#define H_ICACHE_SYNCHRONIZE	(1UL<<(63-41))	/* dcbst, icbi, etc (ignored for IO pages */
#define H_COALESCE_CAND	(1UL<<(63-42))	/* page is a good candidate for coalescing */
#define H_ZERO_PAGE		(1UL<<(63-48))	/* zero the page before mapping (ignored for IO pages) */
#define H_COPY_PAGE		(1UL<<(63-49))
#define H_N			(1UL<<(63-61))
#define H_PP1			(1UL<<(63-62))
#define H_PP2			(1UL<<(63-63))

/* Flags for H_REGISTER_VPA subfunction field */
#define H_VPA_FUNC_SHIFT	(63-18)	/* Bit posn of subfunction code */
#define H_VPA_FUNC_MASK		7UL
#define H_VPA_REG_VPA		1UL	/* Register Virtual Processor Area */
#define H_VPA_REG_DTL		2UL	/* Register Dispatch Trace Log */
#define H_VPA_REG_SLB		3UL	/* Register SLB shadow buffer */
#define H_VPA_DEREG_VPA		5UL	/* Deregister Virtual Processor Area */
#define H_VPA_DEREG_DTL		6UL	/* Deregister Dispatch Trace Log */
#define H_VPA_DEREG_SLB		7UL	/* Deregister SLB shadow buffer */

/* VASI States */
#define H_VASI_INVALID          0
#define H_VASI_ENABLED          1
#define H_VASI_ABORTED          2
#define H_VASI_SUSPENDING       3
#define H_VASI_SUSPENDED        4
#define H_VASI_RESUMED          5
#define H_VASI_COMPLETED        6

/* Each control block has to be on a 4K boundary */
#define H_CB_ALIGNMENT          4096

/* pSeries hypervisor opcodes */
#define H_REMOVE		0x04
#define H_ENTER			0x08
#define H_READ			0x0c
#define H_CLEAR_MOD		0x10
#define H_CLEAR_REF		0x14
#define H_PROTECT		0x18
#define H_GET_TCE		0x1c
#define H_PUT_TCE		0x20
#define H_SET_SPRG0		0x24
#define H_SET_DABR		0x28
#define H_PAGE_INIT		0x2c
#define H_SET_ASR		0x30
#define H_ASR_ON		0x34
#define H_ASR_OFF		0x38
#define H_LOGICAL_CI_LOAD	0x3c
#define H_LOGICAL_CI_STORE	0x40
#define H_LOGICAL_CACHE_LOAD	0x44
#define H_LOGICAL_CACHE_STORE	0x48
#define H_LOGICAL_ICBI		0x4c
#define H_LOGICAL_DCBF		0x50
#define H_GET_TERM_CHAR		0x54
#define H_PUT_TERM_CHAR		0x58
#define H_REAL_TO_LOGICAL	0x5c
#define H_HYPERVISOR_DATA	0x60
#define H_EOI			0x64
#define H_CPPR			0x68
#define H_IPI			0x6c
#define H_IPOLL			0x70
#define H_XIRR			0x74
#define H_PERFMON		0x7c
#define H_MIGRATE_DMA		0x78
#define H_REGISTER_VPA		0xDC
#define H_CEDE			0xE0
#define H_CONFER		0xE4
#define H_PROD			0xE8
#define H_GET_PPP		0xEC
#define H_SET_PPP		0xF0
#define H_PURR			0xF4
#define H_PIC			0xF8
#define H_REG_CRQ		0xFC
#define H_FREE_CRQ		0x100
#define H_VIO_SIGNAL		0x104
#define H_SEND_CRQ		0x108
#define H_COPY_RDMA		0x110
#define H_REGISTER_LOGICAL_LAN	0x114
#define H_FREE_LOGICAL_LAN	0x118
#define H_ADD_LOGICAL_LAN_BUFFER 0x11C
#define H_SEND_LOGICAL_LAN	0x120
#define H_BULK_REMOVE		0x124
#define H_MULTICAST_CTRL	0x130
#define H_SET_XDABR		0x134
#define H_STUFF_TCE		0x138
#define H_PUT_TCE_INDIRECT	0x13C
#define H_CHANGE_LOGICAL_LAN_MAC 0x14C
#define H_VTERM_PARTNER_INFO	0x150
#define H_REGISTER_VTERM	0x154
#define H_FREE_VTERM		0x158
#define H_RESET_EVENTS          0x15C
#define H_ALLOC_RESOURCE        0x160
#define H_FREE_RESOURCE         0x164
#define H_MODIFY_QP             0x168
#define H_QUERY_QP              0x16C
#define H_REREGISTER_PMR        0x170
#define H_REGISTER_SMR          0x174
#define H_QUERY_MR              0x178
#define H_QUERY_MW              0x17C
#define H_QUERY_HCA             0x180
#define H_QUERY_PORT            0x184
#define H_MODIFY_PORT           0x188
#define H_DEFINE_AQP1           0x18C
#define H_GET_TRACE_BUFFER      0x190
#define H_DEFINE_AQP0           0x194
#define H_RESIZE_MR             0x198
#define H_ATTACH_MCQP           0x19C
#define H_DETACH_MCQP           0x1A0
#define H_CREATE_RPT            0x1A4
#define H_REMOVE_RPT            0x1A8
#define H_REGISTER_RPAGES       0x1AC
#define H_DISABLE_AND_GETC      0x1B0
#define H_ERROR_DATA            0x1B4
#define H_GET_HCA_INFO          0x1B8
#define H_GET_PERF_COUNT        0x1BC
#define H_MANAGE_TRACE          0x1C0
#define H_FREE_LOGICAL_LAN_BUFFER 0x1D4
#define H_QUERY_INT_STATE       0x1E4
#define H_POLL_PENDING		0x1D8
#define H_ILLAN_ATTRIBUTES	0x244
#define H_MODIFY_HEA_QP		0x250
#define H_QUERY_HEA_QP		0x254
#define H_QUERY_HEA		0x258
#define H_QUERY_HEA_PORT	0x25C
#define H_MODIFY_HEA_PORT	0x260
#define H_REG_BCMC		0x264
#define H_DEREG_BCMC		0x268
#define H_REGISTER_HEA_RPAGES	0x26C
#define H_DISABLE_AND_GET_HEA	0x270
#define H_GET_HEA_INFO		0x274
#define H_ALLOC_HEA_RESOURCE	0x278
#define H_ADD_CONN		0x284
#define H_DEL_CONN		0x288
#define H_JOIN			0x298
#define H_VASI_STATE            0x2A4
#define H_VIOCTL		0x2A8
#define H_ENABLE_CRQ		0x2B0
#define H_GET_EM_PARMS		0x2B8
#define H_SET_MPP		0x2D0
#define H_GET_MPP		0x2D4
#define H_REG_SUB_CRQ		0x2DC
#define H_HOME_NODE_ASSOCIATIVITY 0x2EC
#define H_FREE_SUB_CRQ		0x2E0
#define H_SEND_SUB_CRQ		0x2E4
#define H_SEND_SUB_CRQ_INDIRECT	0x2E8
#define H_BEST_ENERGY		0x2F4
#define H_XIRR_X		0x2FC
#define H_RANDOM		0x300
#define H_COP			0x304
#define H_GET_MPP_X		0x314
#define H_SET_MODE		0x31C
#define MAX_HCALL_OPCODE	H_SET_MODE

/* H_VIOCTL functions */
#define H_GET_VIOA_DUMP_SIZE	0x01
#define H_GET_VIOA_DUMP		0x02
#define H_GET_ILLAN_NUM_VLAN_IDS 0x03
#define H_GET_ILLAN_VLAN_ID_LIST 0x04
#define H_GET_ILLAN_SWITCH_ID	0x05
#define H_DISABLE_MIGRATION	0x06
#define H_ENABLE_MIGRATION	0x07
#define H_GET_PARTNER_INFO	0x08
#define H_GET_PARTNER_WWPN_LIST	0x09
#define H_DISABLE_ALL_VIO_INTS	0x0A
#define H_DISABLE_VIO_INTERRUPT	0x0B
#define H_ENABLE_VIO_INTERRUPT	0x0C


/* Platform specific hcalls, used by KVM */
#define H_RTAS			0xf000

/* "Platform specific hcalls", provided by PHYP */
#define H_GET_24X7_CATALOG_PAGE	0xF078
#define H_GET_24X7_DATA		0xF07C
#define H_GET_PERF_COUNTER_INFO	0xF080

/* Values for 2nd argument to H_SET_MODE */
#define H_SET_MODE_RESOURCE_SET_CIABR		1
#define H_SET_MODE_RESOURCE_SET_DAWR		2
#define H_SET_MODE_RESOURCE_ADDR_TRANS_MODE	3
#define H_SET_MODE_RESOURCE_LE			4

#ifndef __ASSEMBLY__

/**
 * plpar_hcall_norets: - Make a pseries hypervisor call with no return arguments
 * @opcode: The hypervisor call to make.
 *
 * This call supports up to 7 arguments and only returns the status of
 * the hcall. Use this version where possible, its slightly faster than
 * the other plpar_hcalls.
 */
long plpar_hcall_norets(unsigned long opcode, ...);

/**
 * plpar_hcall: - Make a pseries hypervisor call
 * @opcode: The hypervisor call to make.
 * @retbuf: Buffer to store up to 4 return arguments in.
 *
 * This call supports up to 6 arguments and 4 return arguments. Use
 * PLPAR_HCALL_BUFSIZE to size the return argument buffer.
 *
 * Used for all but the craziest of phyp interfaces (see plpar_hcall9)
 */
#define PLPAR_HCALL_BUFSIZE 4
long plpar_hcall(unsigned long opcode, unsigned long *retbuf, ...);

/**
 * plpar_hcall_raw: - Make a hypervisor call without calculating hcall stats
 * @opcode: The hypervisor call to make.
 * @retbuf: Buffer to store up to 4 return arguments in.
 *
 * This call supports up to 6 arguments and 4 return arguments. Use
 * PLPAR_HCALL_BUFSIZE to size the return argument buffer.
 *
 * Used when phyp interface needs to be called in real mode. Similar to
 * plpar_hcall, but plpar_hcall_raw works in real mode and does not
 * calculate hypervisor call statistics.
 */
long plpar_hcall_raw(unsigned long opcode, unsigned long *retbuf, ...);

/**
 * plpar_hcall9: - Make a pseries hypervisor call with up to 9 return arguments
 * @opcode: The hypervisor call to make.
 * @retbuf: Buffer to store up to 9 return arguments in.
 *
 * This call supports up to 9 arguments and 9 return arguments. Use
 * PLPAR_HCALL9_BUFSIZE to size the return argument buffer.
 */
#define PLPAR_HCALL9_BUFSIZE 9
long plpar_hcall9(unsigned long opcode, unsigned long *retbuf, ...);
long plpar_hcall9_raw(unsigned long opcode, unsigned long *retbuf, ...);

/* For hcall instrumentation.  One structure per-hcall, per-CPU */
struct hcall_stats {
	unsigned long	num_calls;	/* number of calls (on this CPU) */
	unsigned long	tb_total;	/* total wall time (mftb) of calls. */
	unsigned long	purr_total;	/* total cpu time (PURR) of calls. */
	unsigned long	tb_start;
	unsigned long	purr_start;
};
#define HCALL_STAT_ARRAY_SIZE	((MAX_HCALL_OPCODE >> 2) + 1)

struct hvcall_mpp_data {
	unsigned long entitled_mem;
	unsigned long mapped_mem;
	unsigned short group_num;
	unsigned short pool_num;
	unsigned char mem_weight;
	unsigned char unallocated_mem_weight;
	unsigned long unallocated_entitlement;  /* value in bytes */
	unsigned long pool_size;
	signed long loan_request;
	unsigned long backing_mem;
};

int h_get_mpp(struct hvcall_mpp_data *);

struct hvcall_mpp_x_data {
	unsigned long coalesced_bytes;
	unsigned long pool_coalesced_bytes;
	unsigned long pool_purr_cycles;
	unsigned long pool_spurr_cycles;
	unsigned long reserved[3];
};

int h_get_mpp_x(struct hvcall_mpp_x_data *mpp_x_data);

static inline unsigned int get_longbusy_msecs(int longbusy_rc)
{
	switch (longbusy_rc) {
	case H_LONG_BUSY_ORDER_1_MSEC:
		return 1;
	case H_LONG_BUSY_ORDER_10_MSEC:
		return 10;
	case H_LONG_BUSY_ORDER_100_MSEC:
		return 100;
	case H_LONG_BUSY_ORDER_1_SEC:
		return 1000;
	case H_LONG_BUSY_ORDER_10_SEC:
		return 10000;
	case H_LONG_BUSY_ORDER_100_SEC:
		return 100000;
	default:
		return 1;
	}
}

#ifdef CONFIG_PPC_PSERIES
extern int CMO_PrPSP;
extern int CMO_SecPSP;
extern unsigned long CMO_PageSize;

static inline int cmo_get_primary_psp(void)
{
	return CMO_PrPSP;
}

static inline int cmo_get_secondary_psp(void)
{
	return CMO_SecPSP;
}

static inline unsigned long cmo_get_page_size(void)
{
	return CMO_PageSize;
}
#endif /* CONFIG_PPC_PSERIES */

#endif /* __ASSEMBLY__ */
#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_HVCALL_H */
