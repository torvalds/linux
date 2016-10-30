#if !defined(_TRACE_KVM_HV_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_KVM_HV_H

#include <linux/tracepoint.h>
#include "trace_book3s.h"
#include <asm/hvcall.h>
#include <asm/kvm_asm.h>

#undef TRACE_SYSTEM
#define TRACE_SYSTEM kvm_hv
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE trace_hv

#define kvm_trace_symbol_hcall \
	{H_REMOVE,			"H_REMOVE"}, \
	{H_ENTER,			"H_ENTER"}, \
	{H_READ,			"H_READ"}, \
	{H_CLEAR_MOD,			"H_CLEAR_MOD"}, \
	{H_CLEAR_REF,			"H_CLEAR_REF"}, \
	{H_PROTECT,			"H_PROTECT"}, \
	{H_GET_TCE,			"H_GET_TCE"}, \
	{H_PUT_TCE,			"H_PUT_TCE"}, \
	{H_SET_SPRG0,			"H_SET_SPRG0"}, \
	{H_SET_DABR,			"H_SET_DABR"}, \
	{H_PAGE_INIT,			"H_PAGE_INIT"}, \
	{H_SET_ASR,			"H_SET_ASR"}, \
	{H_ASR_ON,			"H_ASR_ON"}, \
	{H_ASR_OFF,			"H_ASR_OFF"}, \
	{H_LOGICAL_CI_LOAD,		"H_LOGICAL_CI_LOAD"}, \
	{H_LOGICAL_CI_STORE,		"H_LOGICAL_CI_STORE"}, \
	{H_LOGICAL_CACHE_LOAD,		"H_LOGICAL_CACHE_LOAD"}, \
	{H_LOGICAL_CACHE_STORE,		"H_LOGICAL_CACHE_STORE"}, \
	{H_LOGICAL_ICBI,		"H_LOGICAL_ICBI"}, \
	{H_LOGICAL_DCBF,		"H_LOGICAL_DCBF"}, \
	{H_GET_TERM_CHAR,		"H_GET_TERM_CHAR"}, \
	{H_PUT_TERM_CHAR,		"H_PUT_TERM_CHAR"}, \
	{H_REAL_TO_LOGICAL,		"H_REAL_TO_LOGICAL"}, \
	{H_HYPERVISOR_DATA,		"H_HYPERVISOR_DATA"}, \
	{H_EOI,				"H_EOI"}, \
	{H_CPPR,			"H_CPPR"}, \
	{H_IPI,				"H_IPI"}, \
	{H_IPOLL,			"H_IPOLL"}, \
	{H_XIRR,			"H_XIRR"}, \
	{H_PERFMON,			"H_PERFMON"}, \
	{H_MIGRATE_DMA,			"H_MIGRATE_DMA"}, \
	{H_REGISTER_VPA,		"H_REGISTER_VPA"}, \
	{H_CEDE,			"H_CEDE"}, \
	{H_CONFER,			"H_CONFER"}, \
	{H_PROD,			"H_PROD"}, \
	{H_GET_PPP,			"H_GET_PPP"}, \
	{H_SET_PPP,			"H_SET_PPP"}, \
	{H_PURR,			"H_PURR"}, \
	{H_PIC,				"H_PIC"}, \
	{H_REG_CRQ,			"H_REG_CRQ"}, \
	{H_FREE_CRQ,			"H_FREE_CRQ"}, \
	{H_VIO_SIGNAL,			"H_VIO_SIGNAL"}, \
	{H_SEND_CRQ,			"H_SEND_CRQ"}, \
	{H_COPY_RDMA,			"H_COPY_RDMA"}, \
	{H_REGISTER_LOGICAL_LAN,	"H_REGISTER_LOGICAL_LAN"}, \
	{H_FREE_LOGICAL_LAN,		"H_FREE_LOGICAL_LAN"}, \
	{H_ADD_LOGICAL_LAN_BUFFER,	"H_ADD_LOGICAL_LAN_BUFFER"}, \
	{H_SEND_LOGICAL_LAN,		"H_SEND_LOGICAL_LAN"}, \
	{H_BULK_REMOVE,			"H_BULK_REMOVE"}, \
	{H_MULTICAST_CTRL,		"H_MULTICAST_CTRL"}, \
	{H_SET_XDABR,			"H_SET_XDABR"}, \
	{H_STUFF_TCE,			"H_STUFF_TCE"}, \
	{H_PUT_TCE_INDIRECT,		"H_PUT_TCE_INDIRECT"}, \
	{H_CHANGE_LOGICAL_LAN_MAC,	"H_CHANGE_LOGICAL_LAN_MAC"}, \
	{H_VTERM_PARTNER_INFO,		"H_VTERM_PARTNER_INFO"}, \
	{H_REGISTER_VTERM,		"H_REGISTER_VTERM"}, \
	{H_FREE_VTERM,			"H_FREE_VTERM"}, \
	{H_RESET_EVENTS,		"H_RESET_EVENTS"}, \
	{H_ALLOC_RESOURCE,		"H_ALLOC_RESOURCE"}, \
	{H_FREE_RESOURCE,		"H_FREE_RESOURCE"}, \
	{H_MODIFY_QP,			"H_MODIFY_QP"}, \
	{H_QUERY_QP,			"H_QUERY_QP"}, \
	{H_REREGISTER_PMR,		"H_REREGISTER_PMR"}, \
	{H_REGISTER_SMR,		"H_REGISTER_SMR"}, \
	{H_QUERY_MR,			"H_QUERY_MR"}, \
	{H_QUERY_MW,			"H_QUERY_MW"}, \
	{H_QUERY_HCA,			"H_QUERY_HCA"}, \
	{H_QUERY_PORT,			"H_QUERY_PORT"}, \
	{H_MODIFY_PORT,			"H_MODIFY_PORT"}, \
	{H_DEFINE_AQP1,			"H_DEFINE_AQP1"}, \
	{H_GET_TRACE_BUFFER,		"H_GET_TRACE_BUFFER"}, \
	{H_DEFINE_AQP0,			"H_DEFINE_AQP0"}, \
	{H_RESIZE_MR,			"H_RESIZE_MR"}, \
	{H_ATTACH_MCQP,			"H_ATTACH_MCQP"}, \
	{H_DETACH_MCQP,			"H_DETACH_MCQP"}, \
	{H_CREATE_RPT,			"H_CREATE_RPT"}, \
	{H_REMOVE_RPT,			"H_REMOVE_RPT"}, \
	{H_REGISTER_RPAGES,		"H_REGISTER_RPAGES"}, \
	{H_DISABLE_AND_GETC,		"H_DISABLE_AND_GETC"}, \
	{H_ERROR_DATA,			"H_ERROR_DATA"}, \
	{H_GET_HCA_INFO,		"H_GET_HCA_INFO"}, \
	{H_GET_PERF_COUNT,		"H_GET_PERF_COUNT"}, \
	{H_MANAGE_TRACE,		"H_MANAGE_TRACE"}, \
	{H_FREE_LOGICAL_LAN_BUFFER,	"H_FREE_LOGICAL_LAN_BUFFER"}, \
	{H_QUERY_INT_STATE,		"H_QUERY_INT_STATE"}, \
	{H_POLL_PENDING,		"H_POLL_PENDING"}, \
	{H_ILLAN_ATTRIBUTES,		"H_ILLAN_ATTRIBUTES"}, \
	{H_MODIFY_HEA_QP,		"H_MODIFY_HEA_QP"}, \
	{H_QUERY_HEA_QP,		"H_QUERY_HEA_QP"}, \
	{H_QUERY_HEA,			"H_QUERY_HEA"}, \
	{H_QUERY_HEA_PORT,		"H_QUERY_HEA_PORT"}, \
	{H_MODIFY_HEA_PORT,		"H_MODIFY_HEA_PORT"}, \
	{H_REG_BCMC,			"H_REG_BCMC"}, \
	{H_DEREG_BCMC,			"H_DEREG_BCMC"}, \
	{H_REGISTER_HEA_RPAGES,		"H_REGISTER_HEA_RPAGES"}, \
	{H_DISABLE_AND_GET_HEA,		"H_DISABLE_AND_GET_HEA"}, \
	{H_GET_HEA_INFO,		"H_GET_HEA_INFO"}, \
	{H_ALLOC_HEA_RESOURCE,		"H_ALLOC_HEA_RESOURCE"}, \
	{H_ADD_CONN,			"H_ADD_CONN"}, \
	{H_DEL_CONN,			"H_DEL_CONN"}, \
	{H_JOIN,			"H_JOIN"}, \
	{H_VASI_STATE,			"H_VASI_STATE"}, \
	{H_ENABLE_CRQ,			"H_ENABLE_CRQ"}, \
	{H_GET_EM_PARMS,		"H_GET_EM_PARMS"}, \
	{H_SET_MPP,			"H_SET_MPP"}, \
	{H_GET_MPP,			"H_GET_MPP"}, \
	{H_HOME_NODE_ASSOCIATIVITY,	"H_HOME_NODE_ASSOCIATIVITY"}, \
	{H_BEST_ENERGY,			"H_BEST_ENERGY"}, \
	{H_XIRR_X,			"H_XIRR_X"}, \
	{H_RANDOM,			"H_RANDOM"}, \
	{H_COP,				"H_COP"}, \
	{H_GET_MPP_X,			"H_GET_MPP_X"}, \
	{H_SET_MODE,			"H_SET_MODE"}, \
	{H_RTAS,			"H_RTAS"}

#define kvm_trace_symbol_kvmret \
	{RESUME_GUEST,			"RESUME_GUEST"}, \
	{RESUME_GUEST_NV,		"RESUME_GUEST_NV"}, \
	{RESUME_HOST,			"RESUME_HOST"}, \
	{RESUME_HOST_NV,		"RESUME_HOST_NV"}

#define kvm_trace_symbol_hcall_rc \
	{H_SUCCESS,			"H_SUCCESS"}, \
	{H_BUSY,			"H_BUSY"}, \
	{H_CLOSED,			"H_CLOSED"}, \
	{H_NOT_AVAILABLE,		"H_NOT_AVAILABLE"}, \
	{H_CONSTRAINED,			"H_CONSTRAINED"}, \
	{H_PARTIAL,			"H_PARTIAL"}, \
	{H_IN_PROGRESS,			"H_IN_PROGRESS"}, \
	{H_PAGE_REGISTERED,		"H_PAGE_REGISTERED"}, \
	{H_PARTIAL_STORE,		"H_PARTIAL_STORE"}, \
	{H_PENDING,			"H_PENDING"}, \
	{H_CONTINUE,			"H_CONTINUE"}, \
	{H_LONG_BUSY_START_RANGE,	"H_LONG_BUSY_START_RANGE"}, \
	{H_LONG_BUSY_ORDER_1_MSEC,	"H_LONG_BUSY_ORDER_1_MSEC"}, \
	{H_LONG_BUSY_ORDER_10_MSEC,	"H_LONG_BUSY_ORDER_10_MSEC"}, \
	{H_LONG_BUSY_ORDER_100_MSEC,	"H_LONG_BUSY_ORDER_100_MSEC"}, \
	{H_LONG_BUSY_ORDER_1_SEC,	"H_LONG_BUSY_ORDER_1_SEC"}, \
	{H_LONG_BUSY_ORDER_10_SEC,	"H_LONG_BUSY_ORDER_10_SEC"}, \
	{H_LONG_BUSY_ORDER_100_SEC,	"H_LONG_BUSY_ORDER_100_SEC"}, \
	{H_LONG_BUSY_END_RANGE,		"H_LONG_BUSY_END_RANGE"}, \
	{H_TOO_HARD,			"H_TOO_HARD"}, \
	{H_HARDWARE,			"H_HARDWARE"}, \
	{H_FUNCTION,			"H_FUNCTION"}, \
	{H_PRIVILEGE,			"H_PRIVILEGE"}, \
	{H_PARAMETER,			"H_PARAMETER"}, \
	{H_BAD_MODE,			"H_BAD_MODE"}, \
	{H_PTEG_FULL,			"H_PTEG_FULL"}, \
	{H_NOT_FOUND,			"H_NOT_FOUND"}, \
	{H_RESERVED_DABR,		"H_RESERVED_DABR"}, \
	{H_NO_MEM,			"H_NO_MEM"}, \
	{H_AUTHORITY,			"H_AUTHORITY"}, \
	{H_PERMISSION,			"H_PERMISSION"}, \
	{H_DROPPED,			"H_DROPPED"}, \
	{H_SOURCE_PARM,			"H_SOURCE_PARM"}, \
	{H_DEST_PARM,			"H_DEST_PARM"}, \
	{H_REMOTE_PARM,			"H_REMOTE_PARM"}, \
	{H_RESOURCE,			"H_RESOURCE"}, \
	{H_ADAPTER_PARM,		"H_ADAPTER_PARM"}, \
	{H_RH_PARM,			"H_RH_PARM"}, \
	{H_RCQ_PARM,			"H_RCQ_PARM"}, \
	{H_SCQ_PARM,			"H_SCQ_PARM"}, \
	{H_EQ_PARM,			"H_EQ_PARM"}, \
	{H_RT_PARM,			"H_RT_PARM"}, \
	{H_ST_PARM,			"H_ST_PARM"}, \
	{H_SIGT_PARM,			"H_SIGT_PARM"}, \
	{H_TOKEN_PARM,			"H_TOKEN_PARM"}, \
	{H_MLENGTH_PARM,		"H_MLENGTH_PARM"}, \
	{H_MEM_PARM,			"H_MEM_PARM"}, \
	{H_MEM_ACCESS_PARM,		"H_MEM_ACCESS_PARM"}, \
	{H_ATTR_PARM,			"H_ATTR_PARM"}, \
	{H_PORT_PARM,			"H_PORT_PARM"}, \
	{H_MCG_PARM,			"H_MCG_PARM"}, \
	{H_VL_PARM,			"H_VL_PARM"}, \
	{H_TSIZE_PARM,			"H_TSIZE_PARM"}, \
	{H_TRACE_PARM,			"H_TRACE_PARM"}, \
	{H_MASK_PARM,			"H_MASK_PARM"}, \
	{H_MCG_FULL,			"H_MCG_FULL"}, \
	{H_ALIAS_EXIST,			"H_ALIAS_EXIST"}, \
	{H_P_COUNTER,			"H_P_COUNTER"}, \
	{H_TABLE_FULL,			"H_TABLE_FULL"}, \
	{H_ALT_TABLE,			"H_ALT_TABLE"}, \
	{H_MR_CONDITION,		"H_MR_CONDITION"}, \
	{H_NOT_ENOUGH_RESOURCES,	"H_NOT_ENOUGH_RESOURCES"}, \
	{H_R_STATE,			"H_R_STATE"}, \
	{H_RESCINDED,			"H_RESCINDED"}, \
	{H_P2,				"H_P2"}, \
	{H_P3,				"H_P3"}, \
	{H_P4,				"H_P4"}, \
	{H_P5,				"H_P5"}, \
	{H_P6,				"H_P6"}, \
	{H_P7,				"H_P7"}, \
	{H_P8,				"H_P8"}, \
	{H_P9,				"H_P9"}, \
	{H_TOO_BIG,			"H_TOO_BIG"}, \
	{H_OVERLAP,			"H_OVERLAP"}, \
	{H_INTERRUPT,			"H_INTERRUPT"}, \
	{H_BAD_DATA,			"H_BAD_DATA"}, \
	{H_NOT_ACTIVE,			"H_NOT_ACTIVE"}, \
	{H_SG_LIST,			"H_SG_LIST"}, \
	{H_OP_MODE,			"H_OP_MODE"}, \
	{H_COP_HW,			"H_COP_HW"}, \
	{H_UNSUPPORTED_FLAG_START,	"H_UNSUPPORTED_FLAG_START"}, \
	{H_UNSUPPORTED_FLAG_END,	"H_UNSUPPORTED_FLAG_END"}, \
	{H_MULTI_THREADS_ACTIVE,	"H_MULTI_THREADS_ACTIVE"}, \
	{H_OUTSTANDING_COP_OPS,		"H_OUTSTANDING_COP_OPS"}

TRACE_EVENT(kvm_guest_enter,
	TP_PROTO(struct kvm_vcpu *vcpu),
	TP_ARGS(vcpu),

	TP_STRUCT__entry(
		__field(int,		vcpu_id)
		__field(unsigned long,	pc)
		__field(unsigned long,  pending_exceptions)
		__field(u8,		ceded)
	),

	TP_fast_assign(
		__entry->vcpu_id	= vcpu->vcpu_id;
		__entry->pc		= kvmppc_get_pc(vcpu);
		__entry->ceded		= vcpu->arch.ceded;
		__entry->pending_exceptions  = vcpu->arch.pending_exceptions;
	),

	TP_printk("VCPU %d: pc=0x%lx pexcp=0x%lx ceded=%d",
			__entry->vcpu_id,
			__entry->pc,
			__entry->pending_exceptions, __entry->ceded)
);

TRACE_EVENT(kvm_guest_exit,
	TP_PROTO(struct kvm_vcpu *vcpu),
	TP_ARGS(vcpu),

	TP_STRUCT__entry(
		__field(int,		vcpu_id)
		__field(int,		trap)
		__field(unsigned long,	pc)
		__field(unsigned long,	msr)
		__field(u8,		ceded)
	),

	TP_fast_assign(
		__entry->vcpu_id = vcpu->vcpu_id;
		__entry->trap	 = vcpu->arch.trap;
		__entry->ceded	 = vcpu->arch.ceded;
		__entry->pc	 = kvmppc_get_pc(vcpu);
		__entry->msr	 = vcpu->arch.shregs.msr;
	),

	TP_printk("VCPU %d: trap=%s pc=0x%lx msr=0x%lx, ceded=%d",
		__entry->vcpu_id,
		__print_symbolic(__entry->trap, kvm_trace_symbol_exit),
		__entry->pc, __entry->msr, __entry->ceded
	)
);

TRACE_EVENT(kvm_page_fault_enter,
	TP_PROTO(struct kvm_vcpu *vcpu, unsigned long *hptep,
		 struct kvm_memory_slot *memslot, unsigned long ea,
		 unsigned long dsisr),

	TP_ARGS(vcpu, hptep, memslot, ea, dsisr),

	TP_STRUCT__entry(
		__field(int,		vcpu_id)
		__field(unsigned long,	hpte_v)
		__field(unsigned long,	hpte_r)
		__field(unsigned long,	gpte_r)
		__field(unsigned long,	ea)
		__field(u64,		base_gfn)
		__field(u32,		slot_flags)
		__field(u32,		dsisr)
	),

	TP_fast_assign(
		__entry->vcpu_id  = vcpu->vcpu_id;
		__entry->hpte_v	  = hptep[0];
		__entry->hpte_r	  = hptep[1];
		__entry->gpte_r	  = hptep[2];
		__entry->ea	  = ea;
		__entry->dsisr	  = dsisr;
		__entry->base_gfn = memslot ? memslot->base_gfn : -1UL;
		__entry->slot_flags = memslot ? memslot->flags : 0;
	),

	TP_printk("VCPU %d: hpte=0x%lx:0x%lx guest=0x%lx ea=0x%lx,%x slot=0x%llx,0x%x",
		   __entry->vcpu_id,
		   __entry->hpte_v, __entry->hpte_r, __entry->gpte_r,
		   __entry->ea, __entry->dsisr,
		   __entry->base_gfn, __entry->slot_flags)
);

TRACE_EVENT(kvm_page_fault_exit,
	TP_PROTO(struct kvm_vcpu *vcpu, unsigned long *hptep, long ret),

	TP_ARGS(vcpu, hptep, ret),

	TP_STRUCT__entry(
		__field(int,		vcpu_id)
		__field(unsigned long,	hpte_v)
		__field(unsigned long,	hpte_r)
		__field(long,		ret)
	),

	TP_fast_assign(
		__entry->vcpu_id  = vcpu->vcpu_id;
		__entry->hpte_v	= hptep[0];
		__entry->hpte_r	= hptep[1];
		__entry->ret = ret;
	),

	TP_printk("VCPU %d: hpte=0x%lx:0x%lx ret=0x%lx",
		   __entry->vcpu_id,
		   __entry->hpte_v, __entry->hpte_r, __entry->ret)
);

TRACE_EVENT(kvm_hcall_enter,
	TP_PROTO(struct kvm_vcpu *vcpu),

	TP_ARGS(vcpu),

	TP_STRUCT__entry(
		__field(int,		vcpu_id)
		__field(unsigned long,	req)
		__field(unsigned long,	gpr4)
		__field(unsigned long,	gpr5)
		__field(unsigned long,	gpr6)
		__field(unsigned long,	gpr7)
	),

	TP_fast_assign(
		__entry->vcpu_id  = vcpu->vcpu_id;
		__entry->req   = kvmppc_get_gpr(vcpu, 3);
		__entry->gpr4  = kvmppc_get_gpr(vcpu, 4);
		__entry->gpr5  = kvmppc_get_gpr(vcpu, 5);
		__entry->gpr6  = kvmppc_get_gpr(vcpu, 6);
		__entry->gpr7  = kvmppc_get_gpr(vcpu, 7);
	),

	TP_printk("VCPU %d: hcall=%s GPR4-7=0x%lx,0x%lx,0x%lx,0x%lx",
		   __entry->vcpu_id,
		   __print_symbolic(__entry->req, kvm_trace_symbol_hcall),
		   __entry->gpr4, __entry->gpr5, __entry->gpr6, __entry->gpr7)
);

TRACE_EVENT(kvm_hcall_exit,
	TP_PROTO(struct kvm_vcpu *vcpu, int ret),

	TP_ARGS(vcpu, ret),

	TP_STRUCT__entry(
		__field(int,		vcpu_id)
		__field(unsigned long,	ret)
		__field(unsigned long,	hcall_rc)
	),

	TP_fast_assign(
		__entry->vcpu_id  = vcpu->vcpu_id;
		__entry->ret	  = ret;
		__entry->hcall_rc = kvmppc_get_gpr(vcpu, 3);
	),

	TP_printk("VCPU %d: ret=%s hcall_rc=%s",
		   __entry->vcpu_id,
		   __print_symbolic(__entry->ret, kvm_trace_symbol_kvmret),
		   __print_symbolic(__entry->ret & RESUME_FLAG_HOST ?
					H_TOO_HARD : __entry->hcall_rc,
					kvm_trace_symbol_hcall_rc))
);

TRACE_EVENT(kvmppc_run_core,
	TP_PROTO(struct kvmppc_vcore *vc, int where),

	TP_ARGS(vc, where),

	TP_STRUCT__entry(
		__field(int,	n_runnable)
		__field(int,	runner_vcpu)
		__field(int,	where)
		__field(pid_t,	tgid)
	),

	TP_fast_assign(
		__entry->runner_vcpu	= vc->runner->vcpu_id;
		__entry->n_runnable	= vc->n_runnable;
		__entry->where		= where;
		__entry->tgid		= current->tgid;
	),

	TP_printk("%s runner_vcpu==%d runnable=%d tgid=%d",
		    __entry->where ? "Exit" : "Enter",
		    __entry->runner_vcpu, __entry->n_runnable, __entry->tgid)
);

TRACE_EVENT(kvmppc_vcore_blocked,
	TP_PROTO(struct kvmppc_vcore *vc, int where),

	TP_ARGS(vc, where),

	TP_STRUCT__entry(
		__field(int,	n_runnable)
		__field(int,	runner_vcpu)
		__field(int,	where)
		__field(pid_t,	tgid)
	),

	TP_fast_assign(
		__entry->runner_vcpu = vc->runner->vcpu_id;
		__entry->n_runnable  = vc->n_runnable;
		__entry->where       = where;
		__entry->tgid	     = current->tgid;
	),

	TP_printk("%s runner_vcpu=%d runnable=%d tgid=%d",
		   __entry->where ? "Exit" : "Enter",
		   __entry->runner_vcpu, __entry->n_runnable, __entry->tgid)
);

TRACE_EVENT(kvmppc_vcore_wakeup,
	TP_PROTO(int do_sleep, __u64 ns),

	TP_ARGS(do_sleep, ns),

	TP_STRUCT__entry(
		__field(__u64,  ns)
		__field(int,    waited)
		__field(pid_t,  tgid)
	),

	TP_fast_assign(
		__entry->ns     = ns;
		__entry->waited = do_sleep;
		__entry->tgid   = current->tgid;
	),

	TP_printk("%s time %lld ns, tgid=%d",
		__entry->waited ? "wait" : "poll",
		__entry->ns, __entry->tgid)
);

TRACE_EVENT(kvmppc_run_vcpu_enter,
	TP_PROTO(struct kvm_vcpu *vcpu),

	TP_ARGS(vcpu),

	TP_STRUCT__entry(
		__field(int,		vcpu_id)
		__field(pid_t,		tgid)
	),

	TP_fast_assign(
		__entry->vcpu_id  = vcpu->vcpu_id;
		__entry->tgid	  = current->tgid;
	),

	TP_printk("VCPU %d: tgid=%d", __entry->vcpu_id, __entry->tgid)
);

TRACE_EVENT(kvmppc_run_vcpu_exit,
	TP_PROTO(struct kvm_vcpu *vcpu, struct kvm_run *run),

	TP_ARGS(vcpu, run),

	TP_STRUCT__entry(
		__field(int,		vcpu_id)
		__field(int,		exit)
		__field(int,		ret)
	),

	TP_fast_assign(
		__entry->vcpu_id  = vcpu->vcpu_id;
		__entry->exit     = run->exit_reason;
		__entry->ret      = vcpu->arch.ret;
	),

	TP_printk("VCPU %d: exit=%d, ret=%d",
			__entry->vcpu_id, __entry->exit, __entry->ret)
);

#endif /* _TRACE_KVM_HV_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
