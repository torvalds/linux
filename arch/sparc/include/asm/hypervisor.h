/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SPARC64_HYPERVISOR_H
#define _SPARC64_HYPERVISOR_H

/* Sun4v hypervisor interfaces and defines.
 *
 * Hypervisor calls are made via traps to software traps number 0x80
 * and above.  Registers %o0 to %o5 serve as argument, status, and
 * return value registers.
 *
 * There are two kinds of these traps.  First there are the normal
 * "fast traps" which use software trap 0x80 and encode the function
 * to invoke by number in register %o5.  Argument and return value
 * handling is as follows:
 *
 * -----------------------------------------------
 * |  %o5  | function number |     undefined     |
 * |  %o0  |   argument 0    |   return status   |
 * |  %o1  |   argument 1    |   return value 1  |
 * |  %o2  |   argument 2    |   return value 2  |
 * |  %o3  |   argument 3    |   return value 3  |
 * |  %o4  |   argument 4    |   return value 4  |
 * -----------------------------------------------
 *
 * The second type are "hyper-fast traps" which encode the function
 * number in the software trap number itself.  So these use trap
 * numbers > 0x80.  The register usage for hyper-fast traps is as
 * follows:
 *
 * -----------------------------------------------
 * |  %o0  |   argument 0    |   return status   |
 * |  %o1  |   argument 1    |   return value 1  |
 * |  %o2  |   argument 2    |   return value 2  |
 * |  %o3  |   argument 3    |   return value 3  |
 * |  %o4  |   argument 4    |   return value 4  |
 * -----------------------------------------------
 *
 * Registers providing explicit arguments to the hypervisor calls
 * are volatile across the call.  Upon return their values are
 * undefined unless explicitly specified as containing a particular
 * return value by the specific call.  The return status is always
 * returned in register %o0, zero indicates a successful execution of
 * the hypervisor call and other values indicate an error status as
 * defined below.  So, for example, if a hyper-fast trap takes
 * arguments 0, 1, and 2, then %o0, %o1, and %o2 are volatile across
 * the call and %o3, %o4, and %o5 would be preserved.
 *
 * If the hypervisor trap is invalid, or the fast trap function number
 * is invalid, HV_EBADTRAP will be returned in %o0.  Also, all 64-bits
 * of the argument and return values are significant.
 */

/* Trap numbers.  */
#define HV_FAST_TRAP		0x80
#define HV_MMU_MAP_ADDR_TRAP	0x83
#define HV_MMU_UNMAP_ADDR_TRAP	0x84
#define HV_TTRACE_ADDENTRY_TRAP	0x85
#define HV_CORE_TRAP		0xff

/* Error codes.  */
#define HV_EOK				0  /* Successful return            */
#define HV_ENOCPU			1  /* Invalid CPU id               */
#define HV_ENORADDR			2  /* Invalid real address         */
#define HV_ENOINTR			3  /* Invalid interrupt id         */
#define HV_EBADPGSZ			4  /* Invalid pagesize encoding    */
#define HV_EBADTSB			5  /* Invalid TSB description      */
#define HV_EINVAL			6  /* Invalid argument             */
#define HV_EBADTRAP			7  /* Invalid function number      */
#define HV_EBADALIGN			8  /* Invalid address alignment    */
#define HV_EWOULDBLOCK			9  /* Cannot complete w/o blocking */
#define HV_ENOACCESS			10 /* No access to resource        */
#define HV_EIO				11 /* I/O error                    */
#define HV_ECPUERROR			12 /* CPU in error state           */
#define HV_ENOTSUPPORTED		13 /* Function not supported       */
#define HV_ENOMAP			14 /* No mapping found             */
#define HV_ETOOMANY			15 /* Too many items specified     */
#define HV_ECHANNEL			16 /* Invalid LDC channel          */
#define HV_EBUSY			17 /* Resource busy                */
#define HV_EUNAVAILABLE			23 /* Resource or operation not
					    * currently available, but may
					    * become available in the future
					    */

/* mach_exit()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_MACH_EXIT
 * ARG0:	exit code
 * ERRORS:	This service does not return.
 *
 * Stop all CPUs in the virtual domain and place them into the stopped
 * state.  The 64-bit exit code may be passed to a service entity as
 * the domain's exit status.  On systems without a service entity, the
 * domain will undergo a reset, and the boot firmware will be
 * reloaded.
 *
 * This function will never return to the guest that invokes it.
 *
 * Note: By convention an exit code of zero denotes a successful exit by
 *       the guest code.  A non-zero exit code denotes a guest specific
 *       error indication.
 *
 */
#define HV_FAST_MACH_EXIT		0x00

#ifndef __ASSEMBLY__
void sun4v_mach_exit(unsigned long exit_code);
#endif

/* Domain services.  */

/* mach_desc()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_MACH_DESC
 * ARG0:	buffer
 * ARG1:	length
 * RET0:	status
 * RET1:	length
 * ERRORS:	HV_EBADALIGN	Buffer is badly aligned
 *		HV_ENORADDR	Buffer is to an illegal real address.
 *		HV_EINVAL	Buffer length is too small for complete
 *				machine description.
 *
 * Copy the most current machine description into the buffer indicated
 * by the real address in ARG0.  The buffer provided must be 16 byte
 * aligned.  Upon success or HV_EINVAL, this service returns the
 * actual size of the machine description in the RET1 return value.
 *
 * Note: A method of determining the appropriate buffer size for the
 *       machine description is to first call this service with a buffer
 *       length of 0 bytes.
 */
#define HV_FAST_MACH_DESC		0x01

#ifndef __ASSEMBLY__
unsigned long sun4v_mach_desc(unsigned long buffer_pa,
			      unsigned long buf_len,
			      unsigned long *real_buf_len);
#endif

/* mach_sir()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_MACH_SIR
 * ERRORS:	This service does not return.
 *
 * Perform a software initiated reset of the virtual machine domain.
 * All CPUs are captured as soon as possible, all hardware devices are
 * returned to the entry default state, and the domain is restarted at
 * the SIR (trap type 0x04) real trap table (RTBA) entry point on one
 * of the CPUs.  The single CPU restarted is selected as determined by
 * platform specific policy.  Memory is preserved across this
 * operation.
 */
#define HV_FAST_MACH_SIR		0x02

#ifndef __ASSEMBLY__
void sun4v_mach_sir(void);
#endif

/* mach_set_watchdog()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_MACH_SET_WATCHDOG
 * ARG0:	timeout in milliseconds
 * RET0:	status
 * RET1:	time remaining in milliseconds
 *
 * A guest uses this API to set a watchdog timer.  Once the gues has set
 * the timer, it must call the timer service again either to disable or
 * postpone the expiration.  If the timer expires before being reset or
 * disabled, then the hypervisor take a platform specific action leading
 * to guest termination within a bounded time period.  The platform action
 * may include recovery actions such as reporting the expiration to a
 * Service Processor, and/or automatically restarting the gues.
 *
 * The 'timeout' parameter is specified in milliseconds, however the
 * implementated granularity is given by the 'watchdog-resolution'
 * property in the 'platform' node of the guest's machine description.
 * The largest allowed timeout value is specified by the
 * 'watchdog-max-timeout' property of the 'platform' node.
 *
 * If the 'timeout' argument is not zero, the watchdog timer is set to
 * expire after a minimum of 'timeout' milliseconds.
 *
 * If the 'timeout' argument is zero, the watchdog timer is disabled.
 *
 * If the 'timeout' value exceeds the value of the 'max-watchdog-timeout'
 * property, the hypervisor leaves the watchdog timer state unchanged,
 * and returns a status of EINVAL.
 *
 * The 'time remaining' return value is valid regardless of whether the
 * return status is EOK or EINVAL.  A non-zero return value indicates the
 * number of milliseconds that were remaining until the timer was to expire.
 * If less than one millisecond remains, the return value is '1'.  If the
 * watchdog timer was disabled at the time of the call, the return value is
 * zero.
 *
 * If the hypervisor cannot support the exact timeout value requested, but
 * can support a larger timeout value, the hypervisor may round the actual
 * timeout to a value larger than the requested timeout, consequently the
 * 'time remaining' return value may be larger than the previously requested
 * timeout value.
 *
 * Any guest OS debugger should be aware that the watchdog service may be in
 * use.  Consequently, it is recommended that the watchdog service is
 * disabled upon debugger entry (e.g. reaching a breakpoint), and then
 * re-enabled upon returning to normal execution.  The API has been designed
 * with this in mind, and the 'time remaining' result of the disable call may
 * be used directly as the timeout argument of the re-enable call.
 */
#define HV_FAST_MACH_SET_WATCHDOG	0x05

#ifndef __ASSEMBLY__
unsigned long sun4v_mach_set_watchdog(unsigned long timeout,
				      unsigned long *orig_timeout);
#endif

/* CPU services.
 *
 * CPUs represent devices that can execute software threads.  A single
 * chip that contains multiple cores or strands is represented as
 * multiple CPUs with unique CPU identifiers.  CPUs are exported to
 * OBP via the machine description (and to the OS via the OBP device
 * tree).  CPUs are always in one of three states: stopped, running,
 * or error.
 *
 * A CPU ID is a pre-assigned 16-bit value that uniquely identifies a
 * CPU within a logical domain.  Operations that are to be performed
 * on multiple CPUs specify them via a CPU list.  A CPU list is an
 * array in real memory, of which each 16-bit word is a CPU ID.  CPU
 * lists are passed through the API as two arguments.  The first is
 * the number of entries (16-bit words) in the CPU list, and the
 * second is the (real address) pointer to the CPU ID list.
 */

/* cpu_start()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_CPU_START
 * ARG0:	CPU ID
 * ARG1:	PC
 * ARG2:	RTBA
 * ARG3:	target ARG0
 * RET0:	status
 * ERRORS:	ENOCPU		Invalid CPU ID
 *		EINVAL		Target CPU ID is not in the stopped state
 *		ENORADDR	Invalid PC or RTBA real address
 *		EBADALIGN	Unaligned PC or unaligned RTBA
 *		EWOULDBLOCK	Starting resources are not available
 *
 * Start CPU with given CPU ID with PC in %pc and with a real trap
 * base address value of RTBA.  The indicated CPU must be in the
 * stopped state.  The supplied RTBA must be aligned on a 256 byte
 * boundary.  On successful completion, the specified CPU will be in
 * the running state and will be supplied with "target ARG0" in %o0
 * and RTBA in %tba.
 */
#define HV_FAST_CPU_START		0x10

#ifndef __ASSEMBLY__
unsigned long sun4v_cpu_start(unsigned long cpuid,
			      unsigned long pc,
			      unsigned long rtba,
			      unsigned long arg0);
#endif

/* cpu_stop()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_CPU_STOP
 * ARG0:	CPU ID
 * RET0:	status
 * ERRORS:	ENOCPU		Invalid CPU ID
 *		EINVAL		Target CPU ID is the current cpu
 *		EINVAL		Target CPU ID is not in the running state
 *		EWOULDBLOCK	Stopping resources are not available
 *		ENOTSUPPORTED	Not supported on this platform
 *
 * The specified CPU is stopped.  The indicated CPU must be in the
 * running state.  On completion, it will be in the stopped state.  It
 * is not legal to stop the current CPU.
 *
 * Note: As this service cannot be used to stop the current cpu, this service
 *       may not be used to stop the last running CPU in a domain.  To stop
 *       and exit a running domain, a guest must use the mach_exit() service.
 */
#define HV_FAST_CPU_STOP		0x11

#ifndef __ASSEMBLY__
unsigned long sun4v_cpu_stop(unsigned long cpuid);
#endif

/* cpu_yield()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_CPU_YIELD
 * RET0:	status
 * ERRORS:	No possible error.
 *
 * Suspend execution on the current CPU.  Execution will resume when
 * an interrupt (device, %stick_compare, or cross-call) is targeted to
 * the CPU.  On some CPUs, this API may be used by the hypervisor to
 * save power by disabling hardware strands.
 */
#define HV_FAST_CPU_YIELD		0x12

#ifndef __ASSEMBLY__
unsigned long sun4v_cpu_yield(void);
#endif

/* cpu_poke()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_CPU_POKE
 * RET0:	status
 * ERRORS:	ENOCPU		cpuid refers to a CPU that does not exist
 *		EINVAL		cpuid is current CPU
 *
 * Poke CPU cpuid. If the target CPU is currently suspended having
 * invoked the cpu-yield service, that vCPU will be resumed.
 * Poke interrupts may only be sent to valid, non-local CPUs.
 * It is not legal to poke the current vCPU.
 */
#define HV_FAST_CPU_POKE                0x13

#ifndef __ASSEMBLY__
unsigned long sun4v_cpu_poke(unsigned long cpuid);
#endif

/* cpu_qconf()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_CPU_QCONF
 * ARG0:	queue
 * ARG1:	base real address
 * ARG2:	number of entries
 * RET0:	status
 * ERRORS:	ENORADDR	Invalid base real address
 *		EINVAL		Invalid queue or number of entries is less
 *				than 2 or too large.
 *		EBADALIGN	Base real address is not correctly aligned
 *				for size.
 *
 * Configure the given queue to be placed at the given base real
 * address, with the given number of entries.  The number of entries
 * must be a power of 2.  The base real address must be aligned
 * exactly to match the queue size.  Each queue entry is 64 bytes
 * long, so for example a 32 entry queue must be aligned on a 2048
 * byte real address boundary.
 *
 * The specified queue is unconfigured if the number of entries is given
 * as zero.
 *
 * For the current version of this API service, the argument queue is defined
 * as follows:
 *
 *	queue		description
 *	-----		-------------------------
 *	0x3c		cpu mondo queue
 *	0x3d		device mondo queue
 *	0x3e		resumable error queue
 *	0x3f		non-resumable error queue
 *
 * Note: The maximum number of entries for each queue for a specific cpu may
 *       be determined from the machine description.
 */
#define HV_FAST_CPU_QCONF		0x14
#define  HV_CPU_QUEUE_CPU_MONDO		 0x3c
#define  HV_CPU_QUEUE_DEVICE_MONDO	 0x3d
#define  HV_CPU_QUEUE_RES_ERROR		 0x3e
#define  HV_CPU_QUEUE_NONRES_ERROR	 0x3f

#ifndef __ASSEMBLY__
unsigned long sun4v_cpu_qconf(unsigned long type,
			      unsigned long queue_paddr,
			      unsigned long num_queue_entries);
#endif

/* cpu_qinfo()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_CPU_QINFO
 * ARG0:	queue
 * RET0:	status
 * RET1:	base real address
 * RET1:	number of entries
 * ERRORS:	EINVAL		Invalid queue
 *
 * Return the configuration info for the given queue.  The base real
 * address and number of entries of the defined queue are returned.
 * The queue argument values are the same as for cpu_qconf() above.
 *
 * If the specified queue is a valid queue number, but no queue has
 * been defined, the number of entries will be set to zero and the
 * base real address returned is undefined.
 */
#define HV_FAST_CPU_QINFO		0x15

/* cpu_mondo_send()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_CPU_MONDO_SEND
 * ARG0-1:	CPU list
 * ARG2:	data real address
 * RET0:	status
 * ERRORS:	EBADALIGN	Mondo data is not 64-byte aligned or CPU list
 *				is not 2-byte aligned.
 *		ENORADDR	Invalid data mondo address, or invalid cpu list
 *				address.
 *		ENOCPU		Invalid cpu in CPU list
 *		EWOULDBLOCK	Some or all of the listed CPUs did not receive
 *				the mondo
 *		ECPUERROR	One or more of the listed CPUs are in error
 *				state, use HV_FAST_CPU_STATE to see which ones
 *		EINVAL		CPU list includes caller's CPU ID
 *
 * Send a mondo interrupt to the CPUs in the given CPU list with the
 * 64-bytes at the given data real address.  The data must be 64-byte
 * aligned.  The mondo data will be delivered to the cpu_mondo queues
 * of the recipient CPUs.
 *
 * In all cases, error or not, the CPUs in the CPU list to which the
 * mondo has been successfully delivered will be indicated by having
 * their entry in CPU list updated with the value 0xffff.
 */
#define HV_FAST_CPU_MONDO_SEND		0x42

#ifndef __ASSEMBLY__
unsigned long sun4v_cpu_mondo_send(unsigned long cpu_count,
				   unsigned long cpu_list_pa,
				   unsigned long mondo_block_pa);
#endif

/* cpu_myid()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_CPU_MYID
 * RET0:	status
 * RET1:	CPU ID
 * ERRORS:	No errors defined.
 *
 * Return the hypervisor ID handle for the current CPU.  Use by a
 * virtual CPU to discover it's own identity.
 */
#define HV_FAST_CPU_MYID		0x16

/* cpu_state()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_CPU_STATE
 * ARG0:	CPU ID
 * RET0:	status
 * RET1:	state
 * ERRORS:	ENOCPU		Invalid CPU ID
 *
 * Retrieve the current state of the CPU with the given CPU ID.
 */
#define HV_FAST_CPU_STATE		0x17
#define  HV_CPU_STATE_STOPPED		 0x01
#define  HV_CPU_STATE_RUNNING		 0x02
#define  HV_CPU_STATE_ERROR		 0x03

#ifndef __ASSEMBLY__
long sun4v_cpu_state(unsigned long cpuid);
#endif

/* cpu_set_rtba()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_CPU_SET_RTBA
 * ARG0:	RTBA
 * RET0:	status
 * RET1:	previous RTBA
 * ERRORS:	ENORADDR	Invalid RTBA real address
 *		EBADALIGN	RTBA is incorrectly aligned for a trap table
 *
 * Set the real trap base address of the local cpu to the given RTBA.
 * The supplied RTBA must be aligned on a 256 byte boundary.  Upon
 * success the previous value of the RTBA is returned in RET1.
 *
 * Note: This service does not affect %tba
 */
#define HV_FAST_CPU_SET_RTBA		0x18

/* cpu_set_rtba()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_CPU_GET_RTBA
 * RET0:	status
 * RET1:	previous RTBA
 * ERRORS:	No possible error.
 *
 * Returns the current value of RTBA in RET1.
 */
#define HV_FAST_CPU_GET_RTBA		0x19

/* MMU services.
 *
 * Layout of a TSB description for mmu_tsb_ctx{,non}0() calls.
 */
#ifndef __ASSEMBLY__
struct hv_tsb_descr {
	unsigned short		pgsz_idx;
	unsigned short		assoc;
	unsigned int		num_ttes;	/* in TTEs */
	unsigned int		ctx_idx;
	unsigned int		pgsz_mask;
	unsigned long		tsb_base;
	unsigned long		resv;
};
#endif
#define HV_TSB_DESCR_PGSZ_IDX_OFFSET	0x00
#define HV_TSB_DESCR_ASSOC_OFFSET	0x02
#define HV_TSB_DESCR_NUM_TTES_OFFSET	0x04
#define HV_TSB_DESCR_CTX_IDX_OFFSET	0x08
#define HV_TSB_DESCR_PGSZ_MASK_OFFSET	0x0c
#define HV_TSB_DESCR_TSB_BASE_OFFSET	0x10
#define HV_TSB_DESCR_RESV_OFFSET	0x18

/* Page size bitmask.  */
#define HV_PGSZ_MASK_8K			(1 << 0)
#define HV_PGSZ_MASK_64K		(1 << 1)
#define HV_PGSZ_MASK_512K		(1 << 2)
#define HV_PGSZ_MASK_4MB		(1 << 3)
#define HV_PGSZ_MASK_32MB		(1 << 4)
#define HV_PGSZ_MASK_256MB		(1 << 5)
#define HV_PGSZ_MASK_2GB		(1 << 6)
#define HV_PGSZ_MASK_16GB		(1 << 7)

/* Page size index.  The value given in the TSB descriptor must correspond
 * to the smallest page size specified in the pgsz_mask page size bitmask.
 */
#define HV_PGSZ_IDX_8K			0
#define HV_PGSZ_IDX_64K			1
#define HV_PGSZ_IDX_512K		2
#define HV_PGSZ_IDX_4MB			3
#define HV_PGSZ_IDX_32MB		4
#define HV_PGSZ_IDX_256MB		5
#define HV_PGSZ_IDX_2GB			6
#define HV_PGSZ_IDX_16GB		7

/* MMU fault status area.
 *
 * MMU related faults have their status and fault address information
 * placed into a memory region made available by privileged code.  Each
 * virtual processor must make a mmu_fault_area_conf() call to tell the
 * hypervisor where that processor's fault status should be stored.
 *
 * The fault status block is a multiple of 64-bytes and must be aligned
 * on a 64-byte boundary.
 */
#ifndef __ASSEMBLY__
struct hv_fault_status {
	unsigned long		i_fault_type;
	unsigned long		i_fault_addr;
	unsigned long		i_fault_ctx;
	unsigned long		i_reserved[5];
	unsigned long		d_fault_type;
	unsigned long		d_fault_addr;
	unsigned long		d_fault_ctx;
	unsigned long		d_reserved[5];
};
#endif
#define HV_FAULT_I_TYPE_OFFSET	0x00
#define HV_FAULT_I_ADDR_OFFSET	0x08
#define HV_FAULT_I_CTX_OFFSET	0x10
#define HV_FAULT_D_TYPE_OFFSET	0x40
#define HV_FAULT_D_ADDR_OFFSET	0x48
#define HV_FAULT_D_CTX_OFFSET	0x50

#define HV_FAULT_TYPE_FAST_MISS	1
#define HV_FAULT_TYPE_FAST_PROT	2
#define HV_FAULT_TYPE_MMU_MISS	3
#define HV_FAULT_TYPE_INV_RA	4
#define HV_FAULT_TYPE_PRIV_VIOL	5
#define HV_FAULT_TYPE_PROT_VIOL	6
#define HV_FAULT_TYPE_NFO	7
#define HV_FAULT_TYPE_NFO_SEFF	8
#define HV_FAULT_TYPE_INV_VA	9
#define HV_FAULT_TYPE_INV_ASI	10
#define HV_FAULT_TYPE_NC_ATOMIC	11
#define HV_FAULT_TYPE_PRIV_ACT	12
#define HV_FAULT_TYPE_RESV1	13
#define HV_FAULT_TYPE_UNALIGNED	14
#define HV_FAULT_TYPE_INV_PGSZ	15
/* Values 16 --> -2 are reserved.  */
#define HV_FAULT_TYPE_MULTIPLE	-1

/* Flags argument for mmu_{map,unmap}_addr(), mmu_demap_{page,context,all}(),
 * and mmu_{map,unmap}_perm_addr().
 */
#define HV_MMU_DMMU			0x01
#define HV_MMU_IMMU			0x02
#define HV_MMU_ALL			(HV_MMU_DMMU | HV_MMU_IMMU)

/* mmu_map_addr()
 * TRAP:	HV_MMU_MAP_ADDR_TRAP
 * ARG0:	virtual address
 * ARG1:	mmu context
 * ARG2:	TTE
 * ARG3:	flags (HV_MMU_{IMMU,DMMU})
 * ERRORS:	EINVAL		Invalid virtual address, mmu context, or flags
 *		EBADPGSZ	Invalid page size value
 *		ENORADDR	Invalid real address in TTE
 *
 * Create a non-permanent mapping using the given TTE, virtual
 * address, and mmu context.  The flags argument determines which
 * (data, or instruction, or both) TLB the mapping gets loaded into.
 *
 * The behavior is undefined if the valid bit is clear in the TTE.
 *
 * Note: This API call is for privileged code to specify temporary translation
 *       mappings without the need to create and manage a TSB.
 */

/* mmu_unmap_addr()
 * TRAP:	HV_MMU_UNMAP_ADDR_TRAP
 * ARG0:	virtual address
 * ARG1:	mmu context
 * ARG2:	flags (HV_MMU_{IMMU,DMMU})
 * ERRORS:	EINVAL		Invalid virtual address, mmu context, or flags
 *
 * Demaps the given virtual address in the given mmu context on this
 * CPU.  This function is intended to be used to demap pages mapped
 * with mmu_map_addr.  This service is equivalent to invoking
 * mmu_demap_page() with only the current CPU in the CPU list. The
 * flags argument determines which (data, or instruction, or both) TLB
 * the mapping gets unmapped from.
 *
 * Attempting to perform an unmap operation for a previously defined
 * permanent mapping will have undefined results.
 */

/* mmu_tsb_ctx0()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_MMU_TSB_CTX0
 * ARG0:	number of TSB descriptions
 * ARG1:	TSB descriptions pointer
 * RET0:	status
 * ERRORS:	ENORADDR		Invalid TSB descriptions pointer or
 *					TSB base within a descriptor
 *		EBADALIGN		TSB descriptions pointer is not aligned
 *					to an 8-byte boundary, or TSB base
 *					within a descriptor is not aligned for
 *					the given TSB size
 *		EBADPGSZ		Invalid page size in a TSB descriptor
 *		EBADTSB			Invalid associativity or size in a TSB
 *					descriptor
 *		EINVAL			Invalid number of TSB descriptions, or
 *					invalid context index in a TSB
 *					descriptor, or index page size not
 *					equal to smallest page size in page
 *					size bitmask field.
 *
 * Configures the TSBs for the current CPU for virtual addresses with
 * context zero.  The TSB descriptions pointer is a pointer to an
 * array of the given number of TSB descriptions.
 *
 * Note: The maximum number of TSBs available to a virtual CPU is given by the
 *       mmu-max-#tsbs property of the cpu's corresponding "cpu" node in the
 *       machine description.
 */
#define HV_FAST_MMU_TSB_CTX0		0x20

#ifndef __ASSEMBLY__
unsigned long sun4v_mmu_tsb_ctx0(unsigned long num_descriptions,
				 unsigned long tsb_desc_ra);
#endif

/* mmu_tsb_ctxnon0()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_MMU_TSB_CTXNON0
 * ARG0:	number of TSB descriptions
 * ARG1:	TSB descriptions pointer
 * RET0:	status
 * ERRORS:	Same as for mmu_tsb_ctx0() above.
 *
 * Configures the TSBs for the current CPU for virtual addresses with
 * non-zero contexts.  The TSB descriptions pointer is a pointer to an
 * array of the given number of TSB descriptions.
 *
 * Note: A maximum of 16 TSBs may be specified in the TSB description list.
 */
#define HV_FAST_MMU_TSB_CTXNON0		0x21

/* mmu_demap_page()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_MMU_DEMAP_PAGE
 * ARG0:	reserved, must be zero
 * ARG1:	reserved, must be zero
 * ARG2:	virtual address
 * ARG3:	mmu context
 * ARG4:	flags (HV_MMU_{IMMU,DMMU})
 * RET0:	status
 * ERRORS:	EINVAL			Invalid virtual address, context, or
 *					flags value
 *		ENOTSUPPORTED		ARG0 or ARG1 is non-zero
 *
 * Demaps any page mapping of the given virtual address in the given
 * mmu context for the current virtual CPU.  Any virtually tagged
 * caches are guaranteed to be kept consistent.  The flags argument
 * determines which TLB (instruction, or data, or both) participate in
 * the operation.
 *
 * ARG0 and ARG1 are both reserved and must be set to zero.
 */
#define HV_FAST_MMU_DEMAP_PAGE		0x22

/* mmu_demap_ctx()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_MMU_DEMAP_CTX
 * ARG0:	reserved, must be zero
 * ARG1:	reserved, must be zero
 * ARG2:	mmu context
 * ARG3:	flags (HV_MMU_{IMMU,DMMU})
 * RET0:	status
 * ERRORS:	EINVAL			Invalid context or flags value
 *		ENOTSUPPORTED		ARG0 or ARG1 is non-zero
 *
 * Demaps all non-permanent virtual page mappings previously specified
 * for the given context for the current virtual CPU.  Any virtual
 * tagged caches are guaranteed to be kept consistent.  The flags
 * argument determines which TLB (instruction, or data, or both)
 * participate in the operation.
 *
 * ARG0 and ARG1 are both reserved and must be set to zero.
 */
#define HV_FAST_MMU_DEMAP_CTX		0x23

/* mmu_demap_all()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_MMU_DEMAP_ALL
 * ARG0:	reserved, must be zero
 * ARG1:	reserved, must be zero
 * ARG2:	flags (HV_MMU_{IMMU,DMMU})
 * RET0:	status
 * ERRORS:	EINVAL			Invalid flags value
 *		ENOTSUPPORTED		ARG0 or ARG1 is non-zero
 *
 * Demaps all non-permanent virtual page mappings previously specified
 * for the current virtual CPU.  Any virtual tagged caches are
 * guaranteed to be kept consistent.  The flags argument determines
 * which TLB (instruction, or data, or both) participate in the
 * operation.
 *
 * ARG0 and ARG1 are both reserved and must be set to zero.
 */
#define HV_FAST_MMU_DEMAP_ALL		0x24

#ifndef __ASSEMBLY__
void sun4v_mmu_demap_all(void);
#endif

/* mmu_map_perm_addr()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_MMU_MAP_PERM_ADDR
 * ARG0:	virtual address
 * ARG1:	reserved, must be zero
 * ARG2:	TTE
 * ARG3:	flags (HV_MMU_{IMMU,DMMU})
 * RET0:	status
 * ERRORS:	EINVAL			Invalid virtual address or flags value
 *		EBADPGSZ		Invalid page size value
 *		ENORADDR		Invalid real address in TTE
 *		ETOOMANY		Too many mappings (max of 8 reached)
 *
 * Create a permanent mapping using the given TTE and virtual address
 * for context 0 on the calling virtual CPU.  A maximum of 8 such
 * permanent mappings may be specified by privileged code.  Mappings
 * may be removed with mmu_unmap_perm_addr().
 *
 * The behavior is undefined if a TTE with the valid bit clear is given.
 *
 * Note: This call is used to specify address space mappings for which
 *       privileged code does not expect to receive misses.  For example,
 *       this mechanism can be used to map kernel nucleus code and data.
 */
#define HV_FAST_MMU_MAP_PERM_ADDR	0x25

#ifndef __ASSEMBLY__
unsigned long sun4v_mmu_map_perm_addr(unsigned long vaddr,
				      unsigned long set_to_zero,
				      unsigned long tte,
				      unsigned long flags);
#endif

/* mmu_fault_area_conf()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_MMU_FAULT_AREA_CONF
 * ARG0:	real address
 * RET0:	status
 * RET1:	previous mmu fault area real address
 * ERRORS:	ENORADDR		Invalid real address
 *		EBADALIGN		Invalid alignment for fault area
 *
 * Configure the MMU fault status area for the calling CPU.  A 64-byte
 * aligned real address specifies where MMU fault status information
 * is placed.  The return value is the previously specified area, or 0
 * for the first invocation.  Specifying a fault area at real address
 * 0 is not allowed.
 */
#define HV_FAST_MMU_FAULT_AREA_CONF	0x26

/* mmu_enable()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_MMU_ENABLE
 * ARG0:	enable flag
 * ARG1:	return target address
 * RET0:	status
 * ERRORS:	ENORADDR		Invalid real address when disabling
 *					translation.
 *		EBADALIGN		The return target address is not
 *					aligned to an instruction.
 *		EINVAL			The enable flag request the current
 *					operating mode (e.g. disable if already
 *					disabled)
 *
 * Enable or disable virtual address translation for the calling CPU
 * within the virtual machine domain.  If the enable flag is zero,
 * translation is disabled, any non-zero value will enable
 * translation.
 *
 * When this function returns, the newly selected translation mode
 * will be active.  If the mmu is being enabled, then the return
 * target address is a virtual address else it is a real address.
 *
 * Upon successful completion, control will be returned to the given
 * return target address (ie. the cpu will jump to that address).  On
 * failure, the previous mmu mode remains and the trap simply returns
 * as normal with the appropriate error code in RET0.
 */
#define HV_FAST_MMU_ENABLE		0x27

/* mmu_unmap_perm_addr()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_MMU_UNMAP_PERM_ADDR
 * ARG0:	virtual address
 * ARG1:	reserved, must be zero
 * ARG2:	flags (HV_MMU_{IMMU,DMMU})
 * RET0:	status
 * ERRORS:	EINVAL			Invalid virtual address or flags value
 *		ENOMAP			Specified mapping was not found
 *
 * Demaps any permanent page mapping (established via
 * mmu_map_perm_addr()) at the given virtual address for context 0 on
 * the current virtual CPU.  Any virtual tagged caches are guaranteed
 * to be kept consistent.
 */
#define HV_FAST_MMU_UNMAP_PERM_ADDR	0x28

/* mmu_tsb_ctx0_info()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_MMU_TSB_CTX0_INFO
 * ARG0:	max TSBs
 * ARG1:	buffer pointer
 * RET0:	status
 * RET1:	number of TSBs
 * ERRORS:	EINVAL			Supplied buffer is too small
 *		EBADALIGN		The buffer pointer is badly aligned
 *		ENORADDR		Invalid real address for buffer pointer
 *
 * Return the TSB configuration as previous defined by mmu_tsb_ctx0()
 * into the provided buffer.  The size of the buffer is given in ARG1
 * in terms of the number of TSB description entries.
 *
 * Upon return, RET1 always contains the number of TSB descriptions
 * previously configured.  If zero TSBs were configured, EOK is
 * returned with RET1 containing 0.
 */
#define HV_FAST_MMU_TSB_CTX0_INFO	0x29

/* mmu_tsb_ctxnon0_info()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_MMU_TSB_CTXNON0_INFO
 * ARG0:	max TSBs
 * ARG1:	buffer pointer
 * RET0:	status
 * RET1:	number of TSBs
 * ERRORS:	EINVAL			Supplied buffer is too small
 *		EBADALIGN		The buffer pointer is badly aligned
 *		ENORADDR		Invalid real address for buffer pointer
 *
 * Return the TSB configuration as previous defined by
 * mmu_tsb_ctxnon0() into the provided buffer.  The size of the buffer
 * is given in ARG1 in terms of the number of TSB description entries.
 *
 * Upon return, RET1 always contains the number of TSB descriptions
 * previously configured.  If zero TSBs were configured, EOK is
 * returned with RET1 containing 0.
 */
#define HV_FAST_MMU_TSB_CTXNON0_INFO	0x2a

/* mmu_fault_area_info()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_MMU_FAULT_AREA_INFO
 * RET0:	status
 * RET1:	fault area real address
 * ERRORS:	No errors defined.
 *
 * Return the currently defined MMU fault status area for the current
 * CPU.  The real address of the fault status area is returned in
 * RET1, or 0 is returned in RET1 if no fault status area is defined.
 *
 * Note: mmu_fault_area_conf() may be called with the return value (RET1)
 *       from this service if there is a need to save and restore the fault
 *	 area for a cpu.
 */
#define HV_FAST_MMU_FAULT_AREA_INFO	0x2b

/* Cache and Memory services. */

/* mem_scrub()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_MEM_SCRUB
 * ARG0:	real address
 * ARG1:	length
 * RET0:	status
 * RET1:	length scrubbed
 * ERRORS:	ENORADDR	Invalid real address
 *		EBADALIGN	Start address or length are not correctly
 *				aligned
 *		EINVAL		Length is zero
 *
 * Zero the memory contents in the range real address to real address
 * plus length minus 1.  Also, valid ECC will be generated for that
 * memory address range.  Scrubbing is started at the given real
 * address, but may not scrub the entire given length.  The actual
 * length scrubbed will be returned in RET1.
 *
 * The real address and length must be aligned on an 8K boundary, or
 * contain the start address and length from a sun4v error report.
 *
 * Note: There are two uses for this function.  The first use is to block clear
 *       and initialize memory and the second is to scrub an u ncorrectable
 *       error reported via a resumable or non-resumable trap.  The second
 *       use requires the arguments to be equal to the real address and length
 *       provided in a sun4v memory error report.
 */
#define HV_FAST_MEM_SCRUB		0x31

/* mem_sync()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_MEM_SYNC
 * ARG0:	real address
 * ARG1:	length
 * RET0:	status
 * RET1:	length synced
 * ERRORS:	ENORADDR	Invalid real address
 *		EBADALIGN	Start address or length are not correctly
 *				aligned
 *		EINVAL		Length is zero
 *
 * Force the next access within the real address to real address plus
 * length minus 1 to be fetches from main system memory.  Less than
 * the given length may be synced, the actual amount synced is
 * returned in RET1.  The real address and length must be aligned on
 * an 8K boundary.
 */
#define HV_FAST_MEM_SYNC		0x32

/* Coprocessor services
 *
 * M7 and later processors provide an on-chip coprocessor which
 * accelerates database operations, and is known internally as
 * DAX.
 */

/* ccb_submit()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_CCB_SUBMIT
 * ARG0:	address of CCB array
 * ARG1:	size (in bytes) of CCB array being submitted
 * ARG2:	flags
 * ARG3:	reserved
 * RET0:	status (success or error code)
 * RET1:	size (in bytes) of CCB array that was accepted (might be less
 *		than arg1)
 * RET2:	status data
 *		if status == ENOMAP or ENOACCESS, identifies the VA in question
 *		if status == EUNAVAILBLE, unavailable code
 * RET3:	reserved
 *
 * ERRORS:	EOK		successful submission (check size)
 *		EWOULDBLOCK	could not finish submissions, try again
 *		EBADALIGN	array not 64B aligned or size not 64B multiple
 *		ENORADDR	invalid RA for array or in CCB
 *		ENOMAP		could not translate address (see status data)
 *		EINVAL		invalid ccb or arguments
 *		ETOOMANY	too many ccbs with all-or-nothing flag
 *		ENOACCESS	guest has no access to submit ccbs or address
 *				in CCB does not have correct permissions (check
 *				status data)
 *		EUNAVAILABLE	ccb operation could not be performed at this
 *				time (check status data)
 *				Status data codes:
 *					0 - exact CCB could not be executed
 *					1 - CCB opcode cannot be executed
 *					2 - CCB version cannot be executed
 *					3 - vcpu cannot execute CCBs
 *					4 - no CCBs can be executed
 */

#define HV_CCB_SUBMIT               0x34
#ifndef __ASSEMBLY__
unsigned long sun4v_ccb_submit(unsigned long ccb_buf,
			       unsigned long len,
			       unsigned long flags,
			       unsigned long reserved,
			       void *submitted_len,
			       void *status_data);
#endif

/* flags (ARG2) */
#define HV_CCB_QUERY_CMD		BIT(1)
#define HV_CCB_ARG0_TYPE_REAL		0UL
#define HV_CCB_ARG0_TYPE_PRIMARY	BIT(4)
#define HV_CCB_ARG0_TYPE_SECONDARY	BIT(5)
#define HV_CCB_ARG0_TYPE_NUCLEUS	GENMASK(5, 4)
#define HV_CCB_ARG0_PRIVILEGED		BIT(6)
#define HV_CCB_ALL_OR_NOTHING		BIT(7)
#define HV_CCB_QUEUE_INFO		BIT(8)
#define HV_CCB_VA_REJECT		0UL
#define HV_CCB_VA_SECONDARY		BIT(13)
#define HV_CCB_VA_NUCLEUS		GENMASK(13, 12)
#define HV_CCB_VA_PRIVILEGED		BIT(14)
#define HV_CCB_VA_READ_ADI_DISABLE	BIT(15)	/* DAX2 only */

/* ccb_info()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_CCB_INFO
 * ARG0:	real address of CCB completion area
 * RET0:	status (success or error code)
 * RET1:	info array
 *			- RET1[0]: CCB state
 *			- RET1[1]: dax unit
 *			- RET1[2]: queue number
 *			- RET1[3]: queue position
 *
 * ERRORS:	EOK		operation successful
 *		EBADALIGN	address not 64B aligned
 *		ENORADDR	RA in address not valid
 *		EINVAL		CA not valid
 *		EWOULDBLOCK	info not available for this CCB currently, try
 *				again
 *		ENOACCESS	guest cannot use dax
 */

#define HV_CCB_INFO                 0x35
#ifndef __ASSEMBLY__
unsigned long sun4v_ccb_info(unsigned long ca,
			     void *info_arr);
#endif

/* info array byte offsets (RET1) */
#define CCB_INFO_OFFSET_CCB_STATE	0
#define CCB_INFO_OFFSET_DAX_UNIT	2
#define CCB_INFO_OFFSET_QUEUE_NUM	4
#define CCB_INFO_OFFSET_QUEUE_POS	6

/* CCB state (RET1[0]) */
#define HV_CCB_STATE_COMPLETED      0
#define HV_CCB_STATE_ENQUEUED       1
#define HV_CCB_STATE_INPROGRESS     2
#define HV_CCB_STATE_NOTFOUND       3

/* ccb_kill()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_CCB_KILL
 * ARG0:	real address of CCB completion area
 * RET0:	status (success or error code)
 * RET1:	CCB kill status
 *
 * ERRORS:	EOK		operation successful
 *		EBADALIGN	address not 64B aligned
 *		ENORADDR	RA in address not valid
 *		EINVAL		CA not valid
 *		EWOULDBLOCK	kill not available for this CCB currently, try
 *				again
 *		ENOACCESS	guest cannot use dax
 */

#define HV_CCB_KILL                 0x36
#ifndef __ASSEMBLY__
unsigned long sun4v_ccb_kill(unsigned long ca,
			     void *kill_status);
#endif

/* CCB kill status (RET1) */
#define HV_CCB_KILL_COMPLETED       0
#define HV_CCB_KILL_DEQUEUED        1
#define HV_CCB_KILL_KILLED          2
#define HV_CCB_KILL_NOTFOUND        3

/* Time of day services.
 *
 * The hypervisor maintains the time of day on a per-domain basis.
 * Changing the time of day in one domain does not affect the time of
 * day on any other domain.
 *
 * Time is described by a single unsigned 64-bit word which is the
 * number of seconds since the UNIX Epoch (00:00:00 UTC, January 1,
 * 1970).
 */

/* tod_get()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_TOD_GET
 * RET0:	status
 * RET1:	TOD
 * ERRORS:	EWOULDBLOCK	TOD resource is temporarily unavailable
 *		ENOTSUPPORTED	If TOD not supported on this platform
 *
 * Return the current time of day.  May block if TOD access is
 * temporarily not possible.
 */
#define HV_FAST_TOD_GET			0x50

#ifndef __ASSEMBLY__
unsigned long sun4v_tod_get(unsigned long *time);
#endif

/* tod_set()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_TOD_SET
 * ARG0:	TOD
 * RET0:	status
 * ERRORS:	EWOULDBLOCK	TOD resource is temporarily unavailable
 *		ENOTSUPPORTED	If TOD not supported on this platform
 *
 * The current time of day is set to the value specified in ARG0.  May
 * block if TOD access is temporarily not possible.
 */
#define HV_FAST_TOD_SET			0x51

#ifndef __ASSEMBLY__
unsigned long sun4v_tod_set(unsigned long time);
#endif

/* Console services */

/* con_getchar()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_CONS_GETCHAR
 * RET0:	status
 * RET1:	character
 * ERRORS:	EWOULDBLOCK	No character available.
 *
 * Returns a character from the console device.  If no character is
 * available then an EWOULDBLOCK error is returned.  If a character is
 * available, then the returned status is EOK and the character value
 * is in RET1.
 *
 * A virtual BREAK is represented by the 64-bit value -1.
 *
 * A virtual HUP signal is represented by the 64-bit value -2.
 */
#define HV_FAST_CONS_GETCHAR		0x60

/* con_putchar()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_CONS_PUTCHAR
 * ARG0:	character
 * RET0:	status
 * ERRORS:	EINVAL		Illegal character
 *		EWOULDBLOCK	Output buffer currently full, would block
 *
 * Send a character to the console device.  Only character values
 * between 0 and 255 may be used.  Values outside this range are
 * invalid except for the 64-bit value -1 which is used to send a
 * virtual BREAK.
 */
#define HV_FAST_CONS_PUTCHAR		0x61

/* con_read()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_CONS_READ
 * ARG0:	buffer real address
 * ARG1:	buffer size in bytes
 * RET0:	status
 * RET1:	bytes read or BREAK or HUP
 * ERRORS:	EWOULDBLOCK	No character available.
 *
 * Reads characters into a buffer from the console device.  If no
 * character is available then an EWOULDBLOCK error is returned.
 * If a character is available, then the returned status is EOK
 * and the number of bytes read into the given buffer is provided
 * in RET1.
 *
 * A virtual BREAK is represented by the 64-bit RET1 value -1.
 *
 * A virtual HUP signal is represented by the 64-bit RET1 value -2.
 *
 * If BREAK or HUP are indicated, no bytes were read into buffer.
 */
#define HV_FAST_CONS_READ		0x62

/* con_write()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_CONS_WRITE
 * ARG0:	buffer real address
 * ARG1:	buffer size in bytes
 * RET0:	status
 * RET1:	bytes written
 * ERRORS:	EWOULDBLOCK	Output buffer currently full, would block
 *
 * Send a characters in buffer to the console device.  Breaks must be
 * sent using con_putchar().
 */
#define HV_FAST_CONS_WRITE		0x63

#ifndef __ASSEMBLY__
long sun4v_con_getchar(long *status);
long sun4v_con_putchar(long c);
long sun4v_con_read(unsigned long buffer,
		    unsigned long size,
		    unsigned long *bytes_read);
unsigned long sun4v_con_write(unsigned long buffer,
			      unsigned long size,
			      unsigned long *bytes_written);
#endif

/* mach_set_soft_state()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_MACH_SET_SOFT_STATE
 * ARG0:	software state
 * ARG1:	software state description pointer
 * RET0:	status
 * ERRORS:	EINVAL		software state not valid or software state
 *				description is not NULL terminated
 *		ENORADDR	software state description pointer is not a
 *				valid real address
 *		EBADALIGNED	software state description is not correctly
 *				aligned
 *
 * This allows the guest to report it's soft state to the hypervisor.  There
 * are two primary components to this state.  The first part states whether
 * the guest software is running or not.  The second containts optional
 * details specific to the software.
 *
 * The software state argument is defined below in HV_SOFT_STATE_*, and
 * indicates whether the guest is operating normally or in a transitional
 * state.
 *
 * The software state description argument is a real address of a data buffer
 * of size 32-bytes aligned on a 32-byte boundary.  It is treated as a NULL
 * terminated 7-bit ASCII string of up to 31 characters not including the
 * NULL termination.
 */
#define HV_FAST_MACH_SET_SOFT_STATE	0x70
#define  HV_SOFT_STATE_NORMAL		 0x01
#define  HV_SOFT_STATE_TRANSITION	 0x02

#ifndef __ASSEMBLY__
unsigned long sun4v_mach_set_soft_state(unsigned long soft_state,
				        unsigned long msg_string_ra);
#endif

/* mach_get_soft_state()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_MACH_GET_SOFT_STATE
 * ARG0:	software state description pointer
 * RET0:	status
 * RET1:	software state
 * ERRORS:	ENORADDR	software state description pointer is not a
 *				valid real address
 *		EBADALIGNED	software state description is not correctly
 *				aligned
 *
 * Retrieve the current value of the guest's software state.  The rules
 * for the software state pointer are the same as for mach_set_soft_state()
 * above.
 */
#define HV_FAST_MACH_GET_SOFT_STATE	0x71

/* svc_send()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_SVC_SEND
 * ARG0:	service ID
 * ARG1:	buffer real address
 * ARG2:	buffer size
 * RET0:	STATUS
 * RET1:	sent_bytes
 *
 * Be careful, all output registers are clobbered by this operation,
 * so for example it is not possible to save away a value in %o4
 * across the trap.
 */
#define HV_FAST_SVC_SEND		0x80

/* svc_recv()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_SVC_RECV
 * ARG0:	service ID
 * ARG1:	buffer real address
 * ARG2:	buffer size
 * RET0:	STATUS
 * RET1:	recv_bytes
 *
 * Be careful, all output registers are clobbered by this operation,
 * so for example it is not possible to save away a value in %o4
 * across the trap.
 */
#define HV_FAST_SVC_RECV		0x81

/* svc_getstatus()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_SVC_GETSTATUS
 * ARG0:	service ID
 * RET0:	STATUS
 * RET1:	status bits
 */
#define HV_FAST_SVC_GETSTATUS		0x82

/* svc_setstatus()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_SVC_SETSTATUS
 * ARG0:	service ID
 * ARG1:	bits to set
 * RET0:	STATUS
 */
#define HV_FAST_SVC_SETSTATUS		0x83

/* svc_clrstatus()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_SVC_CLRSTATUS
 * ARG0:	service ID
 * ARG1:	bits to clear
 * RET0:	STATUS
 */
#define HV_FAST_SVC_CLRSTATUS		0x84

#ifndef __ASSEMBLY__
unsigned long sun4v_svc_send(unsigned long svc_id,
			     unsigned long buffer,
			     unsigned long buffer_size,
			     unsigned long *sent_bytes);
unsigned long sun4v_svc_recv(unsigned long svc_id,
			     unsigned long buffer,
			     unsigned long buffer_size,
			     unsigned long *recv_bytes);
unsigned long sun4v_svc_getstatus(unsigned long svc_id,
				  unsigned long *status_bits);
unsigned long sun4v_svc_setstatus(unsigned long svc_id,
				  unsigned long status_bits);
unsigned long sun4v_svc_clrstatus(unsigned long svc_id,
				  unsigned long status_bits);
#endif

/* Trap trace services.
 *
 * The hypervisor provides a trap tracing capability for privileged
 * code running on each virtual CPU.  Privileged code provides a
 * round-robin trap trace queue within which the hypervisor writes
 * 64-byte entries detailing hyperprivileged traps taken n behalf of
 * privileged code.  This is provided as a debugging capability for
 * privileged code.
 *
 * The trap trace control structure is 64-bytes long and placed at the
 * start (offset 0) of the trap trace buffer, and is described as
 * follows:
 */
#ifndef __ASSEMBLY__
struct hv_trap_trace_control {
	unsigned long		head_offset;
	unsigned long		tail_offset;
	unsigned long		__reserved[0x30 / sizeof(unsigned long)];
};
#endif
#define HV_TRAP_TRACE_CTRL_HEAD_OFFSET	0x00
#define HV_TRAP_TRACE_CTRL_TAIL_OFFSET	0x08

/* The head offset is the offset of the most recently completed entry
 * in the trap-trace buffer.  The tail offset is the offset of the
 * next entry to be written.  The control structure is owned and
 * modified by the hypervisor.  A guest may not modify the control
 * structure contents.  Attempts to do so will result in undefined
 * behavior for the guest.
 *
 * Each trap trace buffer entry is laid out as follows:
 */
#ifndef __ASSEMBLY__
struct hv_trap_trace_entry {
	unsigned char	type;		/* Hypervisor or guest entry?	*/
	unsigned char	hpstate;	/* Hyper-privileged state	*/
	unsigned char	tl;		/* Trap level			*/
	unsigned char	gl;		/* Global register level	*/
	unsigned short	tt;		/* Trap type			*/
	unsigned short	tag;		/* Extended trap identifier	*/
	unsigned long	tstate;		/* Trap state			*/
	unsigned long	tick;		/* Tick				*/
	unsigned long	tpc;		/* Trap PC			*/
	unsigned long	f1;		/* Entry specific		*/
	unsigned long	f2;		/* Entry specific		*/
	unsigned long	f3;		/* Entry specific		*/
	unsigned long	f4;		/* Entry specific		*/
};
#endif
#define HV_TRAP_TRACE_ENTRY_TYPE	0x00
#define HV_TRAP_TRACE_ENTRY_HPSTATE	0x01
#define HV_TRAP_TRACE_ENTRY_TL		0x02
#define HV_TRAP_TRACE_ENTRY_GL		0x03
#define HV_TRAP_TRACE_ENTRY_TT		0x04
#define HV_TRAP_TRACE_ENTRY_TAG		0x06
#define HV_TRAP_TRACE_ENTRY_TSTATE	0x08
#define HV_TRAP_TRACE_ENTRY_TICK	0x10
#define HV_TRAP_TRACE_ENTRY_TPC		0x18
#define HV_TRAP_TRACE_ENTRY_F1		0x20
#define HV_TRAP_TRACE_ENTRY_F2		0x28
#define HV_TRAP_TRACE_ENTRY_F3		0x30
#define HV_TRAP_TRACE_ENTRY_F4		0x38

/* The type field is encoded as follows.  */
#define HV_TRAP_TYPE_UNDEF		0x00 /* Entry content undefined     */
#define HV_TRAP_TYPE_HV			0x01 /* Hypervisor trap entry       */
#define HV_TRAP_TYPE_GUEST		0xff /* Added via ttrace_addentry() */

/* ttrace_buf_conf()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_TTRACE_BUF_CONF
 * ARG0:	real address
 * ARG1:	number of entries
 * RET0:	status
 * RET1:	number of entries
 * ERRORS:	ENORADDR	Invalid real address
 *		EINVAL		Size is too small
 *		EBADALIGN	Real address not aligned on 64-byte boundary
 *
 * Requests hypervisor trap tracing and declares a virtual CPU's trap
 * trace buffer to the hypervisor.  The real address supplies the real
 * base address of the trap trace queue and must be 64-byte aligned.
 * Specifying a value of 0 for the number of entries disables trap
 * tracing for the calling virtual CPU.  The buffer allocated must be
 * sized for a power of two number of 64-byte trap trace entries plus
 * an initial 64-byte control structure.
 *
 * This may be invoked any number of times so that a virtual CPU may
 * relocate a trap trace buffer or create "snapshots" of information.
 *
 * If the real address is illegal or badly aligned, then trap tracing
 * is disabled and an error is returned.
 *
 * Upon failure with EINVAL, this service call returns in RET1 the
 * minimum number of buffer entries required.  Upon other failures
 * RET1 is undefined.
 */
#define HV_FAST_TTRACE_BUF_CONF		0x90

/* ttrace_buf_info()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_TTRACE_BUF_INFO
 * RET0:	status
 * RET1:	real address
 * RET2:	size
 * ERRORS:	None defined.
 *
 * Returns the size and location of the previously declared trap-trace
 * buffer.  In the event that no buffer was previously defined, or the
 * buffer is disabled, this call will return a size of zero bytes.
 */
#define HV_FAST_TTRACE_BUF_INFO		0x91

/* ttrace_enable()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_TTRACE_ENABLE
 * ARG0:	enable
 * RET0:	status
 * RET1:	previous enable state
 * ERRORS:	EINVAL		No trap trace buffer currently defined
 *
 * Enable or disable trap tracing, and return the previous enabled
 * state in RET1.  Future systems may define various flags for the
 * enable argument (ARG0), for the moment a guest should pass
 * "(uint64_t) -1" to enable, and "(uint64_t) 0" to disable all
 * tracing - which will ensure future compatibility.
 */
#define HV_FAST_TTRACE_ENABLE		0x92

/* ttrace_freeze()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_TTRACE_FREEZE
 * ARG0:	freeze
 * RET0:	status
 * RET1:	previous freeze state
 * ERRORS:	EINVAL		No trap trace buffer currently defined
 *
 * Freeze or unfreeze trap tracing, returning the previous freeze
 * state in RET1.  A guest should pass a non-zero value to freeze and
 * a zero value to unfreeze all tracing.  The returned previous state
 * is 0 for not frozen and 1 for frozen.
 */
#define HV_FAST_TTRACE_FREEZE		0x93

/* ttrace_addentry()
 * TRAP:	HV_TTRACE_ADDENTRY_TRAP
 * ARG0:	tag (16-bits)
 * ARG1:	data word 0
 * ARG2:	data word 1
 * ARG3:	data word 2
 * ARG4:	data word 3
 * RET0:	status
 * ERRORS:	EINVAL		No trap trace buffer currently defined
 *
 * Add an entry to the trap trace buffer.  Upon return only ARG0/RET0
 * is modified - none of the other registers holding arguments are
 * volatile across this hypervisor service.
 */

/* Core dump services.
 *
 * Since the hypervisor viraulizes and thus obscures a lot of the
 * physical machine layout and state, traditional OS crash dumps can
 * be difficult to diagnose especially when the problem is a
 * configuration error of some sort.
 *
 * The dump services provide an opaque buffer into which the
 * hypervisor can place it's internal state in order to assist in
 * debugging such situations.  The contents are opaque and extremely
 * platform and hypervisor implementation specific.  The guest, during
 * a core dump, requests that the hypervisor update any information in
 * the dump buffer in preparation to being dumped as part of the
 * domain's memory image.
 */

/* dump_buf_update()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_DUMP_BUF_UPDATE
 * ARG0:	real address
 * ARG1:	size
 * RET0:	status
 * RET1:	required size of dump buffer
 * ERRORS:	ENORADDR	Invalid real address
 *		EBADALIGN	Real address is not aligned on a 64-byte
 *				boundary
 *		EINVAL		Size is non-zero but less than minimum size
 *				required
 *		ENOTSUPPORTED	Operation not supported on current logical
 *				domain
 *
 * Declare a domain dump buffer to the hypervisor.  The real address
 * provided for the domain dump buffer must be 64-byte aligned.  The
 * size specifies the size of the dump buffer and may be larger than
 * the minimum size specified in the machine description.  The
 * hypervisor will fill the dump buffer with opaque data.
 *
 * Note: A guest may elect to include dump buffer contents as part of a crash
 *       dump to assist with debugging.  This function may be called any number
 *       of times so that a guest may relocate a dump buffer, or create
 *       "snapshots" of any dump-buffer information.  Each call to
 *       dump_buf_update() atomically declares the new dump buffer to the
 *       hypervisor.
 *
 * A specified size of 0 unconfigures the dump buffer.  If the real
 * address is illegal or badly aligned, then any currently active dump
 * buffer is disabled and an error is returned.
 *
 * In the event that the call fails with EINVAL, RET1 contains the
 * minimum size requires by the hypervisor for a valid dump buffer.
 */
#define HV_FAST_DUMP_BUF_UPDATE		0x94

/* dump_buf_info()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_DUMP_BUF_INFO
 * RET0:	status
 * RET1:	real address of current dump buffer
 * RET2:	size of current dump buffer
 * ERRORS:	No errors defined.
 *
 * Return the currently configures dump buffer description.  A
 * returned size of 0 bytes indicates an undefined dump buffer.  In
 * this case the return address in RET1 is undefined.
 */
#define HV_FAST_DUMP_BUF_INFO		0x95

/* Device interrupt services.
 *
 * Device interrupts are allocated to system bus bridges by the hypervisor,
 * and described to OBP in the machine description.  OBP then describes
 * these interrupts to the OS via properties in the device tree.
 *
 * Terminology:
 *
 *	cpuid		Unique opaque value which represents a target cpu.
 *
 *	devhandle	Device handle.  It uniquely identifies a device, and
 *			consistes of the lower 28-bits of the hi-cell of the
 *			first entry of the device's "reg" property in the
 *			OBP device tree.
 *
 *	devino		Device interrupt number.  Specifies the relative
 *			interrupt number within the device.  The unique
 *			combination of devhandle and devino are used to
 *			identify a specific device interrupt.
 *
 *			Note: The devino value is the same as the values in the
 *			      "interrupts" property or "interrupt-map" property
 *			      in the OBP device tree for that device.
 *
 *	sysino		System interrupt number.  A 64-bit unsigned interger
 *			representing a unique interrupt within a virtual
 *			machine.
 *
 *	intr_state	A flag representing the interrupt state for a given
 *			sysino.  The state values are defined below.
 *
 *	intr_enabled	A flag representing the 'enabled' state for a given
 *			sysino.  The enable values are defined below.
 */

#define HV_INTR_STATE_IDLE		0 /* Nothing pending */
#define HV_INTR_STATE_RECEIVED		1 /* Interrupt received by hardware */
#define HV_INTR_STATE_DELIVERED		2 /* Interrupt delivered to queue */

#define HV_INTR_DISABLED		0 /* sysino not enabled */
#define HV_INTR_ENABLED			1 /* sysino enabled */

/* intr_devino_to_sysino()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_INTR_DEVINO2SYSINO
 * ARG0:	devhandle
 * ARG1:	devino
 * RET0:	status
 * RET1:	sysino
 * ERRORS:	EINVAL		Invalid devhandle/devino
 *
 * Converts a device specific interrupt number of the given
 * devhandle/devino into a system specific ino (sysino).
 */
#define HV_FAST_INTR_DEVINO2SYSINO	0xa0

#ifndef __ASSEMBLY__
unsigned long sun4v_devino_to_sysino(unsigned long devhandle,
				     unsigned long devino);
#endif

/* intr_getenabled()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_INTR_GETENABLED
 * ARG0:	sysino
 * RET0:	status
 * RET1:	intr_enabled (HV_INTR_{DISABLED,ENABLED})
 * ERRORS:	EINVAL		Invalid sysino
 *
 * Returns interrupt enabled state in RET1 for the interrupt defined
 * by the given sysino.
 */
#define HV_FAST_INTR_GETENABLED		0xa1

#ifndef __ASSEMBLY__
unsigned long sun4v_intr_getenabled(unsigned long sysino);
#endif

/* intr_setenabled()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_INTR_SETENABLED
 * ARG0:	sysino
 * ARG1:	intr_enabled (HV_INTR_{DISABLED,ENABLED})
 * RET0:	status
 * ERRORS:	EINVAL		Invalid sysino or intr_enabled value
 *
 * Set the 'enabled' state of the interrupt sysino.
 */
#define HV_FAST_INTR_SETENABLED		0xa2

#ifndef __ASSEMBLY__
unsigned long sun4v_intr_setenabled(unsigned long sysino,
				    unsigned long intr_enabled);
#endif

/* intr_getstate()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_INTR_GETSTATE
 * ARG0:	sysino
 * RET0:	status
 * RET1:	intr_state (HV_INTR_STATE_*)
 * ERRORS:	EINVAL		Invalid sysino
 *
 * Returns current state of the interrupt defined by the given sysino.
 */
#define HV_FAST_INTR_GETSTATE		0xa3

#ifndef __ASSEMBLY__
unsigned long sun4v_intr_getstate(unsigned long sysino);
#endif

/* intr_setstate()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_INTR_SETSTATE
 * ARG0:	sysino
 * ARG1:	intr_state (HV_INTR_STATE_*)
 * RET0:	status
 * ERRORS:	EINVAL		Invalid sysino or intr_state value
 *
 * Sets the current state of the interrupt described by the given sysino
 * value.
 *
 * Note: Setting the state to HV_INTR_STATE_IDLE clears any pending
 *       interrupt for sysino.
 */
#define HV_FAST_INTR_SETSTATE		0xa4

#ifndef __ASSEMBLY__
unsigned long sun4v_intr_setstate(unsigned long sysino, unsigned long intr_state);
#endif

/* intr_gettarget()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_INTR_GETTARGET
 * ARG0:	sysino
 * RET0:	status
 * RET1:	cpuid
 * ERRORS:	EINVAL		Invalid sysino
 *
 * Returns CPU that is the current target of the interrupt defined by
 * the given sysino.  The CPU value returned is undefined if the target
 * has not been set via intr_settarget().
 */
#define HV_FAST_INTR_GETTARGET		0xa5

#ifndef __ASSEMBLY__
unsigned long sun4v_intr_gettarget(unsigned long sysino);
#endif

/* intr_settarget()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_INTR_SETTARGET
 * ARG0:	sysino
 * ARG1:	cpuid
 * RET0:	status
 * ERRORS:	EINVAL		Invalid sysino
 *		ENOCPU		Invalid cpuid
 *
 * Set the target CPU for the interrupt defined by the given sysino.
 */
#define HV_FAST_INTR_SETTARGET		0xa6

#ifndef __ASSEMBLY__
unsigned long sun4v_intr_settarget(unsigned long sysino, unsigned long cpuid);
#endif

/* vintr_get_cookie()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_VINTR_GET_COOKIE
 * ARG0:	device handle
 * ARG1:	device ino
 * RET0:	status
 * RET1:	cookie
 */
#define HV_FAST_VINTR_GET_COOKIE	0xa7

/* vintr_set_cookie()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_VINTR_SET_COOKIE
 * ARG0:	device handle
 * ARG1:	device ino
 * ARG2:	cookie
 * RET0:	status
 */
#define HV_FAST_VINTR_SET_COOKIE	0xa8

/* vintr_get_valid()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_VINTR_GET_VALID
 * ARG0:	device handle
 * ARG1:	device ino
 * RET0:	status
 * RET1:	valid state
 */
#define HV_FAST_VINTR_GET_VALID		0xa9

/* vintr_set_valid()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_VINTR_SET_VALID
 * ARG0:	device handle
 * ARG1:	device ino
 * ARG2:	valid state
 * RET0:	status
 */
#define HV_FAST_VINTR_SET_VALID		0xaa

/* vintr_get_state()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_VINTR_GET_STATE
 * ARG0:	device handle
 * ARG1:	device ino
 * RET0:	status
 * RET1:	state
 */
#define HV_FAST_VINTR_GET_STATE		0xab

/* vintr_set_state()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_VINTR_SET_STATE
 * ARG0:	device handle
 * ARG1:	device ino
 * ARG2:	state
 * RET0:	status
 */
#define HV_FAST_VINTR_SET_STATE		0xac

/* vintr_get_target()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_VINTR_GET_TARGET
 * ARG0:	device handle
 * ARG1:	device ino
 * RET0:	status
 * RET1:	cpuid
 */
#define HV_FAST_VINTR_GET_TARGET	0xad

/* vintr_set_target()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_VINTR_SET_TARGET
 * ARG0:	device handle
 * ARG1:	device ino
 * ARG2:	cpuid
 * RET0:	status
 */
#define HV_FAST_VINTR_SET_TARGET	0xae

#ifndef __ASSEMBLY__
unsigned long sun4v_vintr_get_cookie(unsigned long dev_handle,
				     unsigned long dev_ino,
				     unsigned long *cookie);
unsigned long sun4v_vintr_set_cookie(unsigned long dev_handle,
				     unsigned long dev_ino,
				     unsigned long cookie);
unsigned long sun4v_vintr_get_valid(unsigned long dev_handle,
				    unsigned long dev_ino,
				    unsigned long *valid);
unsigned long sun4v_vintr_set_valid(unsigned long dev_handle,
				    unsigned long dev_ino,
				    unsigned long valid);
unsigned long sun4v_vintr_get_state(unsigned long dev_handle,
				    unsigned long dev_ino,
				    unsigned long *state);
unsigned long sun4v_vintr_set_state(unsigned long dev_handle,
				    unsigned long dev_ino,
				    unsigned long state);
unsigned long sun4v_vintr_get_target(unsigned long dev_handle,
				     unsigned long dev_ino,
				     unsigned long *cpuid);
unsigned long sun4v_vintr_set_target(unsigned long dev_handle,
				     unsigned long dev_ino,
				     unsigned long cpuid);
#endif

/* PCI IO services.
 *
 * See the terminology descriptions in the device interrupt services
 * section above as those apply here too.  Here are terminology
 * definitions specific to these PCI IO services:
 *
 *	tsbnum		TSB number.  Indentifies which io-tsb is used.
 *			For this version of the specification, tsbnum
 *			must be zero.
 *
 *	tsbindex	TSB index.  Identifies which entry in the TSB
 *			is used.  The first entry is zero.
 *
 *	tsbid		A 64-bit aligned data structure which contains
 *			a tsbnum and a tsbindex.  Bits 63:32 contain the
 *			tsbnum and bits 31:00 contain the tsbindex.
 *
 *			Use the HV_PCI_TSBID() macro to construct such
 * 			values.
 *
 *	io_attributes	IO attributes for IOMMU mappings.  One of more
 *			of the attritbute bits are stores in a 64-bit
 *			value.  The values are defined below.
 *
 *	r_addr		64-bit real address
 *
 *	pci_device	PCI device address.  A PCI device address identifies
 *			a specific device on a specific PCI bus segment.
 *			A PCI device address ia a 32-bit unsigned integer
 *			with the following format:
 *
 *				00000000.bbbbbbbb.dddddfff.00000000
 *
 *			Use the HV_PCI_DEVICE_BUILD() macro to construct
 *			such values.
 *
 *	pci_config_offset
 *			PCI configureation space offset.  For conventional
 *			PCI a value between 0 and 255.  For extended
 *			configuration space, a value between 0 and 4095.
 *
 *			Note: For PCI configuration space accesses, the offset
 *			      must be aligned to the access size.
 *
 *	error_flag	A return value which specifies if the action succeeded
 *			or failed.  0 means no error, non-0 means some error
 *			occurred while performing the service.
 *
 *	io_sync_direction
 *			Direction definition for pci_dma_sync(), defined
 *			below in HV_PCI_SYNC_*.
 *
 *	io_page_list	A list of io_page_addresses, an io_page_address is
 *			a real address.
 *
 *	io_page_list_p	A pointer to an io_page_list.
 *
 *	"size based byte swap" - Some functions do size based byte swapping
 *				 which allows sw to access pointers and
 *				 counters in native form when the processor
 *				 operates in a different endianness than the
 *				 IO bus.  Size-based byte swapping converts a
 *				 multi-byte field between big-endian and
 *				 little-endian format.
 */

#define HV_PCI_MAP_ATTR_READ		0x01
#define HV_PCI_MAP_ATTR_WRITE		0x02
#define HV_PCI_MAP_ATTR_RELAXED_ORDER	0x04

#define HV_PCI_DEVICE_BUILD(b,d,f)	\
	((((b) & 0xff) << 16) | \
	 (((d) & 0x1f) << 11) | \
	 (((f) & 0x07) <<  8))

#define HV_PCI_TSBID(__tsb_num, __tsb_index) \
	((((u64)(__tsb_num)) << 32UL) | ((u64)(__tsb_index)))

#define HV_PCI_SYNC_FOR_DEVICE		0x01
#define HV_PCI_SYNC_FOR_CPU		0x02

/* pci_iommu_map()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_PCI_IOMMU_MAP
 * ARG0:	devhandle
 * ARG1:	tsbid
 * ARG2:	#ttes
 * ARG3:	io_attributes
 * ARG4:	io_page_list_p
 * RET0:	status
 * RET1:	#ttes mapped
 * ERRORS:	EINVAL		Invalid devhandle/tsbnum/tsbindex/io_attributes
 *		EBADALIGN	Improperly aligned real address
 *		ENORADDR	Invalid real address
 *
 * Create IOMMU mappings in the sun4v device defined by the given
 * devhandle.  The mappings are created in the TSB defined by the
 * tsbnum component of the given tsbid.  The first mapping is created
 * in the TSB i ndex defined by the tsbindex component of the given tsbid.
 * The call creates up to #ttes mappings, the first one at tsbnum, tsbindex,
 * the second at tsbnum, tsbindex + 1, etc.
 *
 * All mappings are created with the attributes defined by the io_attributes
 * argument.  The page mapping addresses are described in the io_page_list
 * defined by the given io_page_list_p, which is a pointer to the io_page_list.
 * The first entry in the io_page_list is the address for the first iotte, the
 * 2nd for the 2nd iotte, and so on.
 *
 * Each io_page_address in the io_page_list must be appropriately aligned.
 * #ttes must be greater than zero.  For this version of the spec, the tsbnum
 * component of the given tsbid must be zero.
 *
 * Returns the actual number of mappings creates, which may be less than
 * or equal to the argument #ttes.  If the function returns a value which
 * is less than the #ttes, the caller may continus to call the function with
 * an updated tsbid, #ttes, io_page_list_p arguments until all pages are
 * mapped.
 *
 * Note: This function does not imply an iotte cache flush.  The guest must
 *       demap an entry before re-mapping it.
 */
#define HV_FAST_PCI_IOMMU_MAP		0xb0

/* pci_iommu_demap()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_PCI_IOMMU_DEMAP
 * ARG0:	devhandle
 * ARG1:	tsbid
 * ARG2:	#ttes
 * RET0:	status
 * RET1:	#ttes demapped
 * ERRORS:	EINVAL		Invalid devhandle/tsbnum/tsbindex
 *
 * Demap and flush IOMMU mappings in the device defined by the given
 * devhandle.  Demaps up to #ttes entries in the TSB defined by the tsbnum
 * component of the given tsbid, starting at the TSB index defined by the
 * tsbindex component of the given tsbid.
 *
 * For this version of the spec, the tsbnum of the given tsbid must be zero.
 * #ttes must be greater than zero.
 *
 * Returns the actual number of ttes demapped, which may be less than or equal
 * to the argument #ttes.  If #ttes demapped is less than #ttes, the caller
 * may continue to call this function with updated tsbid and #ttes arguments
 * until all pages are demapped.
 *
 * Note: Entries do not have to be mapped to be demapped.  A demap of an
 *       unmapped page will flush the entry from the tte cache.
 */
#define HV_FAST_PCI_IOMMU_DEMAP		0xb1

/* pci_iommu_getmap()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_PCI_IOMMU_GETMAP
 * ARG0:	devhandle
 * ARG1:	tsbid
 * RET0:	status
 * RET1:	io_attributes
 * RET2:	real address
 * ERRORS:	EINVAL		Invalid devhandle/tsbnum/tsbindex
 *		ENOMAP		Mapping is not valid, no translation exists
 *
 * Read and return the mapping in the device described by the given devhandle
 * and tsbid.  If successful, the io_attributes shall be returned in RET1
 * and the page address of the mapping shall be returned in RET2.
 *
 * For this version of the spec, the tsbnum component of the given tsbid
 * must be zero.
 */
#define HV_FAST_PCI_IOMMU_GETMAP	0xb2

/* pci_iommu_getbypass()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_PCI_IOMMU_GETBYPASS
 * ARG0:	devhandle
 * ARG1:	real address
 * ARG2:	io_attributes
 * RET0:	status
 * RET1:	io_addr
 * ERRORS:	EINVAL		Invalid devhandle/io_attributes
 *		ENORADDR	Invalid real address
 *		ENOTSUPPORTED	Function not supported in this implementation.
 *
 * Create a "special" mapping in the device described by the given devhandle,
 * for the given real address and attributes.  Return the IO address in RET1
 * if successful.
 */
#define HV_FAST_PCI_IOMMU_GETBYPASS	0xb3

/* pci_config_get()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_PCI_CONFIG_GET
 * ARG0:	devhandle
 * ARG1:	pci_device
 * ARG2:	pci_config_offset
 * ARG3:	size
 * RET0:	status
 * RET1:	error_flag
 * RET2:	data
 * ERRORS:	EINVAL		Invalid devhandle/pci_device/offset/size
 *		EBADALIGN	pci_config_offset not size aligned
 *		ENOACCESS	Access to this offset is not permitted
 *
 * Read PCI configuration space for the adapter described by the given
 * devhandle.  Read size (1, 2, or 4) bytes of data from the given
 * pci_device, at pci_config_offset from the beginning of the device's
 * configuration space.  If there was no error, RET1 is set to zero and
 * RET2 is set to the data read.  Insignificant bits in RET2 are not
 * guaranteed to have any specific value and therefore must be ignored.
 *
 * The data returned in RET2 is size based byte swapped.
 *
 * If an error occurs during the read, set RET1 to a non-zero value.  The
 * given pci_config_offset must be 'size' aligned.
 */
#define HV_FAST_PCI_CONFIG_GET		0xb4

/* pci_config_put()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_PCI_CONFIG_PUT
 * ARG0:	devhandle
 * ARG1:	pci_device
 * ARG2:	pci_config_offset
 * ARG3:	size
 * ARG4:	data
 * RET0:	status
 * RET1:	error_flag
 * ERRORS:	EINVAL		Invalid devhandle/pci_device/offset/size
 *		EBADALIGN	pci_config_offset not size aligned
 *		ENOACCESS	Access to this offset is not permitted
 *
 * Write PCI configuration space for the adapter described by the given
 * devhandle.  Write size (1, 2, or 4) bytes of data in a single operation,
 * at pci_config_offset from the beginning of the device's configuration
 * space.  The data argument contains the data to be written to configuration
 * space.  Prior to writing, the data is size based byte swapped.
 *
 * If an error occurs during the write access, do not generate an error
 * report, do set RET1 to a non-zero value.  Otherwise RET1 is zero.
 * The given pci_config_offset must be 'size' aligned.
 *
 * This function is permitted to read from offset zero in the configuration
 * space described by the given pci_device if necessary to ensure that the
 * write access to config space completes.
 */
#define HV_FAST_PCI_CONFIG_PUT		0xb5

/* pci_peek()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_PCI_PEEK
 * ARG0:	devhandle
 * ARG1:	real address
 * ARG2:	size
 * RET0:	status
 * RET1:	error_flag
 * RET2:	data
 * ERRORS:	EINVAL		Invalid devhandle or size
 *		EBADALIGN	Improperly aligned real address
 *		ENORADDR	Bad real address
 *		ENOACCESS	Guest access prohibited
 *
 * Attempt to read the IO address given by the given devhandle, real address,
 * and size.  Size must be 1, 2, 4, or 8.  The read is performed as a single
 * access operation using the given size.  If an error occurs when reading
 * from the given location, do not generate an error report, but return a
 * non-zero value in RET1.  If the read was successful, return zero in RET1
 * and return the actual data read in RET2.  The data returned is size based
 * byte swapped.
 *
 * Non-significant bits in RET2 are not guaranteed to have any specific value
 * and therefore must be ignored.  If RET1 is returned as non-zero, the data
 * value is not guaranteed to have any specific value and should be ignored.
 *
 * The caller must have permission to read from the given devhandle, real
 * address, which must be an IO address.  The argument real address must be a
 * size aligned address.
 *
 * The hypervisor implementation of this function must block access to any
 * IO address that the guest does not have explicit permission to access.
 */
#define HV_FAST_PCI_PEEK		0xb6

/* pci_poke()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_PCI_POKE
 * ARG0:	devhandle
 * ARG1:	real address
 * ARG2:	size
 * ARG3:	data
 * ARG4:	pci_device
 * RET0:	status
 * RET1:	error_flag
 * ERRORS:	EINVAL		Invalid devhandle, size, or pci_device
 *		EBADALIGN	Improperly aligned real address
 *		ENORADDR	Bad real address
 *		ENOACCESS	Guest access prohibited
 *		ENOTSUPPORTED	Function is not supported by implementation
 *
 * Attempt to write data to the IO address given by the given devhandle,
 * real address, and size.  Size must be 1, 2, 4, or 8.  The write is
 * performed as a single access operation using the given size. Prior to
 * writing the data is size based swapped.
 *
 * If an error occurs when writing to the given location, do not generate an
 * error report, but return a non-zero value in RET1.  If the write was
 * successful, return zero in RET1.
 *
 * pci_device describes the configuration address of the device being
 * written to.  The implementation may safely read from offset 0 with
 * the configuration space of the device described by devhandle and
 * pci_device in order to guarantee that the write portion of the operation
 * completes
 *
 * Any error that occurs due to the read shall be reported using the normal
 * error reporting mechanisms .. the read error is not suppressed.
 *
 * The caller must have permission to write to the given devhandle, real
 * address, which must be an IO address.  The argument real address must be a
 * size aligned address.  The caller must have permission to read from
 * the given devhandle, pci_device cofiguration space offset 0.
 *
 * The hypervisor implementation of this function must block access to any
 * IO address that the guest does not have explicit permission to access.
 */
#define HV_FAST_PCI_POKE		0xb7

/* pci_dma_sync()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_PCI_DMA_SYNC
 * ARG0:	devhandle
 * ARG1:	real address
 * ARG2:	size
 * ARG3:	io_sync_direction
 * RET0:	status
 * RET1:	#synced
 * ERRORS:	EINVAL		Invalid devhandle or io_sync_direction
 *		ENORADDR	Bad real address
 *
 * Synchronize a memory region described by the given real address and size,
 * for the device defined by the given devhandle using the direction(s)
 * defined by the given io_sync_direction.  The argument size is the size of
 * the memory region in bytes.
 *
 * Return the actual number of bytes synchronized in the return value #synced,
 * which may be less than or equal to the argument size.  If the return
 * value #synced is less than size, the caller must continue to call this
 * function with updated real address and size arguments until the entire
 * memory region is synchronized.
 */
#define HV_FAST_PCI_DMA_SYNC		0xb8

/* PCI MSI services.  */

#define HV_MSITYPE_MSI32		0x00
#define HV_MSITYPE_MSI64		0x01

#define HV_MSIQSTATE_IDLE		0x00
#define HV_MSIQSTATE_ERROR		0x01

#define HV_MSIQ_INVALID			0x00
#define HV_MSIQ_VALID			0x01

#define HV_MSISTATE_IDLE		0x00
#define HV_MSISTATE_DELIVERED		0x01

#define HV_MSIVALID_INVALID		0x00
#define HV_MSIVALID_VALID		0x01

#define HV_PCIE_MSGTYPE_PME_MSG		0x18
#define HV_PCIE_MSGTYPE_PME_ACK_MSG	0x1b
#define HV_PCIE_MSGTYPE_CORR_MSG	0x30
#define HV_PCIE_MSGTYPE_NONFATAL_MSG	0x31
#define HV_PCIE_MSGTYPE_FATAL_MSG	0x33

#define HV_MSG_INVALID			0x00
#define HV_MSG_VALID			0x01

/* pci_msiq_conf()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_PCI_MSIQ_CONF
 * ARG0:	devhandle
 * ARG1:	msiqid
 * ARG2:	real address
 * ARG3:	number of entries
 * RET0:	status
 * ERRORS:	EINVAL		Invalid devhandle, msiqid or nentries
 *		EBADALIGN	Improperly aligned real address
 *		ENORADDR	Bad real address
 *
 * Configure the MSI queue given by the devhandle and msiqid arguments,
 * and to be placed at the given real address and be of the given
 * number of entries.  The real address must be aligned exactly to match
 * the queue size.  Each queue entry is 64-bytes long, so f.e. a 32 entry
 * queue must be aligned on a 2048 byte real address boundary.  The MSI-EQ
 * Head and Tail are initialized so that the MSI-EQ is 'empty'.
 *
 * Implementation Note: Certain implementations have fixed sized queues.  In
 *                      that case, number of entries must contain the correct
 *                      value.
 */
#define HV_FAST_PCI_MSIQ_CONF		0xc0

/* pci_msiq_info()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_PCI_MSIQ_INFO
 * ARG0:	devhandle
 * ARG1:	msiqid
 * RET0:	status
 * RET1:	real address
 * RET2:	number of entries
 * ERRORS:	EINVAL		Invalid devhandle or msiqid
 *
 * Return the configuration information for the MSI queue described
 * by the given devhandle and msiqid.  The base address of the queue
 * is returned in ARG1 and the number of entries is returned in ARG2.
 * If the queue is unconfigured, the real address is undefined and the
 * number of entries will be returned as zero.
 */
#define HV_FAST_PCI_MSIQ_INFO		0xc1

/* pci_msiq_getvalid()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_PCI_MSIQ_GETVALID
 * ARG0:	devhandle
 * ARG1:	msiqid
 * RET0:	status
 * RET1:	msiqvalid	(HV_MSIQ_VALID or HV_MSIQ_INVALID)
 * ERRORS:	EINVAL		Invalid devhandle or msiqid
 *
 * Get the valid state of the MSI-EQ described by the given devhandle and
 * msiqid.
 */
#define HV_FAST_PCI_MSIQ_GETVALID	0xc2

/* pci_msiq_setvalid()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_PCI_MSIQ_SETVALID
 * ARG0:	devhandle
 * ARG1:	msiqid
 * ARG2:	msiqvalid	(HV_MSIQ_VALID or HV_MSIQ_INVALID)
 * RET0:	status
 * ERRORS:	EINVAL		Invalid devhandle or msiqid or msiqvalid
 *				value or MSI EQ is uninitialized
 *
 * Set the valid state of the MSI-EQ described by the given devhandle and
 * msiqid to the given msiqvalid.
 */
#define HV_FAST_PCI_MSIQ_SETVALID	0xc3

/* pci_msiq_getstate()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_PCI_MSIQ_GETSTATE
 * ARG0:	devhandle
 * ARG1:	msiqid
 * RET0:	status
 * RET1:	msiqstate	(HV_MSIQSTATE_IDLE or HV_MSIQSTATE_ERROR)
 * ERRORS:	EINVAL		Invalid devhandle or msiqid
 *
 * Get the state of the MSI-EQ described by the given devhandle and
 * msiqid.
 */
#define HV_FAST_PCI_MSIQ_GETSTATE	0xc4

/* pci_msiq_getvalid()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_PCI_MSIQ_GETVALID
 * ARG0:	devhandle
 * ARG1:	msiqid
 * ARG2:	msiqstate	(HV_MSIQSTATE_IDLE or HV_MSIQSTATE_ERROR)
 * RET0:	status
 * ERRORS:	EINVAL		Invalid devhandle or msiqid or msiqstate
 *				value or MSI EQ is uninitialized
 *
 * Set the state of the MSI-EQ described by the given devhandle and
 * msiqid to the given msiqvalid.
 */
#define HV_FAST_PCI_MSIQ_SETSTATE	0xc5

/* pci_msiq_gethead()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_PCI_MSIQ_GETHEAD
 * ARG0:	devhandle
 * ARG1:	msiqid
 * RET0:	status
 * RET1:	msiqhead
 * ERRORS:	EINVAL		Invalid devhandle or msiqid
 *
 * Get the current MSI EQ queue head for the MSI-EQ described by the
 * given devhandle and msiqid.
 */
#define HV_FAST_PCI_MSIQ_GETHEAD	0xc6

/* pci_msiq_sethead()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_PCI_MSIQ_SETHEAD
 * ARG0:	devhandle
 * ARG1:	msiqid
 * ARG2:	msiqhead
 * RET0:	status
 * ERRORS:	EINVAL		Invalid devhandle or msiqid or msiqhead,
 *				or MSI EQ is uninitialized
 *
 * Set the current MSI EQ queue head for the MSI-EQ described by the
 * given devhandle and msiqid.
 */
#define HV_FAST_PCI_MSIQ_SETHEAD	0xc7

/* pci_msiq_gettail()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_PCI_MSIQ_GETTAIL
 * ARG0:	devhandle
 * ARG1:	msiqid
 * RET0:	status
 * RET1:	msiqtail
 * ERRORS:	EINVAL		Invalid devhandle or msiqid
 *
 * Get the current MSI EQ queue tail for the MSI-EQ described by the
 * given devhandle and msiqid.
 */
#define HV_FAST_PCI_MSIQ_GETTAIL	0xc8

/* pci_msi_getvalid()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_PCI_MSI_GETVALID
 * ARG0:	devhandle
 * ARG1:	msinum
 * RET0:	status
 * RET1:	msivalidstate
 * ERRORS:	EINVAL		Invalid devhandle or msinum
 *
 * Get the current valid/enabled state for the MSI defined by the
 * given devhandle and msinum.
 */
#define HV_FAST_PCI_MSI_GETVALID	0xc9

/* pci_msi_setvalid()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_PCI_MSI_SETVALID
 * ARG0:	devhandle
 * ARG1:	msinum
 * ARG2:	msivalidstate
 * RET0:	status
 * ERRORS:	EINVAL		Invalid devhandle or msinum or msivalidstate
 *
 * Set the current valid/enabled state for the MSI defined by the
 * given devhandle and msinum.
 */
#define HV_FAST_PCI_MSI_SETVALID	0xca

/* pci_msi_getmsiq()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_PCI_MSI_GETMSIQ
 * ARG0:	devhandle
 * ARG1:	msinum
 * RET0:	status
 * RET1:	msiqid
 * ERRORS:	EINVAL		Invalid devhandle or msinum or MSI is unbound
 *
 * Get the MSI EQ that the MSI defined by the given devhandle and
 * msinum is bound to.
 */
#define HV_FAST_PCI_MSI_GETMSIQ		0xcb

/* pci_msi_setmsiq()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_PCI_MSI_SETMSIQ
 * ARG0:	devhandle
 * ARG1:	msinum
 * ARG2:	msitype
 * ARG3:	msiqid
 * RET0:	status
 * ERRORS:	EINVAL		Invalid devhandle or msinum or msiqid
 *
 * Set the MSI EQ that the MSI defined by the given devhandle and
 * msinum is bound to.
 */
#define HV_FAST_PCI_MSI_SETMSIQ		0xcc

/* pci_msi_getstate()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_PCI_MSI_GETSTATE
 * ARG0:	devhandle
 * ARG1:	msinum
 * RET0:	status
 * RET1:	msistate
 * ERRORS:	EINVAL		Invalid devhandle or msinum
 *
 * Get the state of the MSI defined by the given devhandle and msinum.
 * If not initialized, return HV_MSISTATE_IDLE.
 */
#define HV_FAST_PCI_MSI_GETSTATE	0xcd

/* pci_msi_setstate()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_PCI_MSI_SETSTATE
 * ARG0:	devhandle
 * ARG1:	msinum
 * ARG2:	msistate
 * RET0:	status
 * ERRORS:	EINVAL		Invalid devhandle or msinum or msistate
 *
 * Set the state of the MSI defined by the given devhandle and msinum.
 */
#define HV_FAST_PCI_MSI_SETSTATE	0xce

/* pci_msg_getmsiq()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_PCI_MSG_GETMSIQ
 * ARG0:	devhandle
 * ARG1:	msgtype
 * RET0:	status
 * RET1:	msiqid
 * ERRORS:	EINVAL		Invalid devhandle or msgtype
 *
 * Get the MSI EQ of the MSG defined by the given devhandle and msgtype.
 */
#define HV_FAST_PCI_MSG_GETMSIQ		0xd0

/* pci_msg_setmsiq()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_PCI_MSG_SETMSIQ
 * ARG0:	devhandle
 * ARG1:	msgtype
 * ARG2:	msiqid
 * RET0:	status
 * ERRORS:	EINVAL		Invalid devhandle, msgtype, or msiqid
 *
 * Set the MSI EQ of the MSG defined by the given devhandle and msgtype.
 */
#define HV_FAST_PCI_MSG_SETMSIQ		0xd1

/* pci_msg_getvalid()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_PCI_MSG_GETVALID
 * ARG0:	devhandle
 * ARG1:	msgtype
 * RET0:	status
 * RET1:	msgvalidstate
 * ERRORS:	EINVAL		Invalid devhandle or msgtype
 *
 * Get the valid/enabled state of the MSG defined by the given
 * devhandle and msgtype.
 */
#define HV_FAST_PCI_MSG_GETVALID	0xd2

/* pci_msg_setvalid()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_PCI_MSG_SETVALID
 * ARG0:	devhandle
 * ARG1:	msgtype
 * ARG2:	msgvalidstate
 * RET0:	status
 * ERRORS:	EINVAL		Invalid devhandle or msgtype or msgvalidstate
 *
 * Set the valid/enabled state of the MSG defined by the given
 * devhandle and msgtype.
 */
#define HV_FAST_PCI_MSG_SETVALID	0xd3

/* PCI IOMMU v2 definitions and services
 *
 * While the PCI IO definitions above is valid IOMMU v2 adds new PCI IO
 * definitions and services.
 *
 *	CTE		Clump Table Entry. First level table entry in the ATU.
 *
 *	pci_device_list
 *			A 32-bit aligned list of pci_devices.
 *
 *	pci_device_listp
 *			real address of a pci_device_list. 32-bit aligned.
 *
 *	iotte		IOMMU translation table entry.
 *
 *	iotte_attributes
 *			IO Attributes for IOMMU v2 mappings. In addition to
 *			read, write IOMMU v2 supports relax ordering
 *
 *	io_page_list	A 64-bit aligned list of real addresses. Each real
 *			address in an io_page_list must be properly aligned
 *			to the pagesize of the given IOTSB.
 *
 *	io_page_list_p	Real address of an io_page_list, 64-bit aligned.
 *
 *	IOTSB		IO Translation Storage Buffer. An aligned table of
 *			IOTTEs. Each IOTSB has a pagesize, table size, and
 *			virtual address associated with it that must match
 *			a pagesize and table size supported by the un-derlying
 *			hardware implementation. The alignment requirements
 *			for an IOTSB depend on the pagesize used for that IOTSB.
 *			Each IOTTE in an IOTSB maps one pagesize-sized page.
 *			The size of the IOTSB dictates how large of a virtual
 *			address space the IOTSB is capable of mapping.
 *
 *	iotsb_handle	An opaque identifier for an IOTSB. A devhandle plus
 *			iotsb_handle represents a binding of an IOTSB to a
 *			PCI root complex.
 *
 *	iotsb_index	Zero-based IOTTE number within an IOTSB.
 */

/* The index_count argument consists of two fields:
 * bits 63:48 #iottes and bits 47:0 iotsb_index
 */
#define HV_PCI_IOTSB_INDEX_COUNT(__iottes, __iotsb_index) \
	(((u64)(__iottes) << 48UL) | ((u64)(__iotsb_index)))

/* pci_iotsb_conf()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_PCI_IOTSB_CONF
 * ARG0:	devhandle
 * ARG1:	r_addr
 * ARG2:	size
 * ARG3:	pagesize
 * ARG4:	iova
 * RET0:	status
 * RET1:	iotsb_handle
 * ERRORS:	EINVAL		Invalid devhandle, size, iova, or pagesize
 *		EBADALIGN	r_addr is not properly aligned
 *		ENORADDR	r_addr is not a valid real address
 *		ETOOMANY	No further IOTSBs may be configured
 *		EBUSY		Duplicate devhandle, raddir, iova combination
 *
 * Create an IOTSB suitable for the PCI root complex identified by devhandle,
 * for the DMA virtual address defined by the argument iova.
 *
 * r_addr is the properly aligned base address of the IOTSB and size is the
 * IOTSB (table) size in bytes.The IOTSB is required to be zeroed prior to
 * being configured. If it contains any values other than zeros then the
 * behavior is undefined.
 *
 * pagesize is the size of each page in the IOTSB. Note that the combination of
 * size (table size) and pagesize must be valid.
 *
 * virt is the DMA virtual address this IOTSB will map.
 *
 * If successful, the opaque 64-bit handle iotsb_handle is returned in ret1.
 * Once configured, privileged access to the IOTSB memory is prohibited and
 * creates undefined behavior. The only permitted access is indirect via these
 * services.
 */
#define HV_FAST_PCI_IOTSB_CONF		0x190

/* pci_iotsb_info()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_PCI_IOTSB_INFO
 * ARG0:	devhandle
 * ARG1:	iotsb_handle
 * RET0:	status
 * RET1:	r_addr
 * RET2:	size
 * RET3:	pagesize
 * RET4:	iova
 * RET5:	#bound
 * ERRORS:	EINVAL	Invalid devhandle or iotsb_handle
 *
 * This service returns configuration information about an IOTSB previously
 * created with pci_iotsb_conf.
 *
 * iotsb_handle value 0 may be used with this service to inquire about the
 * legacy IOTSB that may or may not exist. If the service succeeds, the return
 * values describe the legacy IOTSB and I/O virtual addresses mapped by that
 * table. However, the table base address r_addr may contain the value -1 which
 * indicates a memory range that cannot be accessed or be reclaimed.
 *
 * The return value #bound contains the number of PCI devices that iotsb_handle
 * is currently bound to.
 */
#define HV_FAST_PCI_IOTSB_INFO		0x191

/* pci_iotsb_unconf()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_PCI_IOTSB_UNCONF
 * ARG0:	devhandle
 * ARG1:	iotsb_handle
 * RET0:	status
 * ERRORS:	EINVAL	Invalid devhandle or iotsb_handle
 *		EBUSY	The IOTSB is bound and may not be unconfigured
 *
 * This service unconfigures the IOTSB identified by the devhandle and
 * iotsb_handle arguments, previously created with pci_iotsb_conf.
 * The IOTSB must not be currently bound to any device or the service will fail
 *
 * If the call succeeds, iotsb_handle is no longer valid.
 */
#define HV_FAST_PCI_IOTSB_UNCONF	0x192

/* pci_iotsb_bind()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_PCI_IOTSB_BIND
 * ARG0:	devhandle
 * ARG1:	iotsb_handle
 * ARG2:	pci_device
 * RET0:	status
 * ERRORS:	EINVAL	Invalid devhandle, iotsb_handle, or pci_device
 *		EBUSY	A PCI function is already bound to an IOTSB at the same
 *			address range as specified by devhandle, iotsb_handle.
 *
 * This service binds the PCI function specified by the argument pci_device to
 * the IOTSB specified by the arguments devhandle and iotsb_handle.
 *
 * The PCI device function is bound to the specified IOTSB with the IOVA range
 * specified when the IOTSB was configured via pci_iotsb_conf. If the function
 * is already bound then it is unbound first.
 */
#define HV_FAST_PCI_IOTSB_BIND		0x193

/* pci_iotsb_unbind()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_PCI_IOTSB_UNBIND
 * ARG0:	devhandle
 * ARG1:	iotsb_handle
 * ARG2:	pci_device
 * RET0:	status
 * ERRORS:	EINVAL	Invalid devhandle, iotsb_handle, or pci_device
 *		ENOMAP	The PCI function was not bound to the specified IOTSB
 *
 * This service unbinds the PCI device specified by the argument pci_device
 * from the IOTSB identified  * by the arguments devhandle and iotsb_handle.
 *
 * If the PCI device is not bound to the specified IOTSB then this service will
 * fail with status ENOMAP
 */
#define HV_FAST_PCI_IOTSB_UNBIND	0x194

/* pci_iotsb_get_binding()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_PCI_IOTSB_GET_BINDING
 * ARG0:	devhandle
 * ARG1:	iotsb_handle
 * ARG2:	iova
 * RET0:	status
 * RET1:	iotsb_handle
 * ERRORS:	EINVAL	Invalid devhandle, pci_device, or iova
 *		ENOMAP	The PCI function is not bound to an IOTSB at iova
 *
 * This service returns the IOTSB binding, iotsb_handle, for a given pci_device
 * and DMA virtual address, iova.
 *
 * iova must be the base address of a DMA virtual address range as defined by
 * the iommu-address-ranges property in the root complex device node defined
 * by the argument devhandle.
 */
#define HV_FAST_PCI_IOTSB_GET_BINDING	0x195

/* pci_iotsb_map()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_PCI_IOTSB_MAP
 * ARG0:	devhandle
 * ARG1:	iotsb_handle
 * ARG2:	index_count
 * ARG3:	iotte_attributes
 * ARG4:	io_page_list_p
 * RET0:	status
 * RET1:	#mapped
 * ERRORS:	EINVAL		Invalid devhandle, iotsb_handle, #iottes,
 *				iotsb_index or iotte_attributes
 *		EBADALIGN	Improperly aligned io_page_list_p or I/O page
 *				address in the I/O page list.
 *		ENORADDR	Invalid io_page_list_p or I/O page address in
 *				the I/O page list.
 *
 * This service creates and flushes mappings in the IOTSB defined by the
 * arguments devhandle, iotsb.
 *
 * The index_count argument consists of two fields. Bits 63:48 contain #iotte
 * and bits 47:0 contain iotsb_index
 *
 * The first mapping is created in the IOTSB index specified by iotsb_index.
 * Subsequent mappings are  created at iotsb_index+1 and so on.
 *
 * The attributes of each mapping are defined by the argument iotte_attributes.
 *
 * The io_page_list_p specifies the real address of the 64-bit-aligned list of
 * #iottes I/O page addresses. Each page address must be a properly aligned
 * real address of a page to be mapped in the IOTSB. The first entry in the I/O
 * page list contains the real address of the first page, the 2nd entry for the
 * 2nd page, and so on.
 *
 * #iottes must be greater than zero.
 *
 * The return value #mapped is the actual number of mappings created, which may
 * be less than or equal to the argument #iottes. If the function returns
 * successfully with a #mapped value less than the requested #iottes then the
 * caller should continue to invoke the service with updated iotsb_index,
 * #iottes, and io_page_list_p arguments until all pages are mapped.
 *
 * This service must not be used to demap a mapping. In other words, all
 * mappings must be valid and have  one or both of the RW attribute bits set.
 *
 * Note:
 * It is implementation-defined whether I/O page real address validity checking
 * is done at time mappings are established or deferred until they are
 * accessed.
 */
#define HV_FAST_PCI_IOTSB_MAP		0x196

/* pci_iotsb_map_one()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_PCI_IOTSB_MAP_ONE
 * ARG0:	devhandle
 * ARG1:	iotsb_handle
 * ARG2:	iotsb_index
 * ARG3:	iotte_attributes
 * ARG4:	r_addr
 * RET0:	status
 * ERRORS:	EINVAL		Invalid devhandle,iotsb_handle, iotsb_index
 *				or iotte_attributes
 *		EBADALIGN	Improperly aligned r_addr
 *		ENORADDR	Invalid r_addr
 *
 * This service creates and flushes a single mapping in the IOTSB defined by the
 * arguments devhandle, iotsb.
 *
 * The mapping for the page at r_addr is created at the IOTSB index specified by
 * iotsb_index with  the attributes iotte_attributes.
 *
 * This service must not be used to demap a mapping. In other words, the mapping
 * must be valid and have one or both of the RW attribute bits set.
 *
 * Note:
 * It is implementation-defined whether I/O page real address validity checking
 * is done at time mappings are established or deferred until they are
 * accessed.
 */
#define HV_FAST_PCI_IOTSB_MAP_ONE	0x197

/* pci_iotsb_demap()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_PCI_IOTSB_DEMAP
 * ARG0:	devhandle
 * ARG1:	iotsb_handle
 * ARG2:	iotsb_index
 * ARG3:	#iottes
 * RET0:	status
 * RET1:	#unmapped
 * ERRORS:	EINVAL	Invalid devhandle, iotsb_handle, iotsb_index or #iottes
 *
 * This service unmaps and flushes up to #iottes mappings starting at index
 * iotsb_index from the IOTSB defined by the arguments devhandle, iotsb.
 *
 * #iottes must be greater than zero.
 *
 * The actual number of IOTTEs unmapped is returned in #unmapped and may be less
 * than or equal to the requested number of IOTTEs, #iottes.
 *
 * If #unmapped is less than #iottes, the caller should continue to invoke this
 * service with updated iotsb_index and #iottes arguments until all pages are
 * demapped.
 */
#define HV_FAST_PCI_IOTSB_DEMAP		0x198

/* pci_iotsb_getmap()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_PCI_IOTSB_GETMAP
 * ARG0:	devhandle
 * ARG1:	iotsb_handle
 * ARG2:	iotsb_index
 * RET0:	status
 * RET1:	r_addr
 * RET2:	iotte_attributes
 * ERRORS:	EINVAL	Invalid devhandle, iotsb_handle, or iotsb_index
 *		ENOMAP	No mapping was found
 *
 * This service returns the mapping specified by index iotsb_index from the
 * IOTSB defined by the arguments devhandle, iotsb.
 *
 * Upon success, the real address of the mapping shall be returned in
 * r_addr and thethe IOTTE mapping attributes shall be returned in
 * iotte_attributes.
 *
 * The return value iotte_attributes may not include optional features used in
 * the call to create the  mapping.
 */
#define HV_FAST_PCI_IOTSB_GETMAP	0x199

/* pci_iotsb_sync_mappings()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_PCI_IOTSB_SYNC_MAPPINGS
 * ARG0:	devhandle
 * ARG1:	iotsb_handle
 * ARG2:	iotsb_index
 * ARG3:	#iottes
 * RET0:	status
 * RET1:	#synced
 * ERROS:	EINVAL	Invalid devhandle, iotsb_handle, iotsb_index, or #iottes
 *
 * This service synchronizes #iottes mappings starting at index iotsb_index in
 * the IOTSB defined by the arguments devhandle, iotsb.
 *
 * #iottes must be greater than zero.
 *
 * The actual number of IOTTEs synchronized is returned in #synced, which may
 * be less than or equal to the requested number, #iottes.
 *
 * Upon a successful return, #synced is less than #iottes, the caller should
 * continue to invoke this service with updated iotsb_index and #iottes
 * arguments until all pages are synchronized.
 */
#define HV_FAST_PCI_IOTSB_SYNC_MAPPINGS	0x19a

/* Logical Domain Channel services.  */

#define LDC_CHANNEL_DOWN		0
#define LDC_CHANNEL_UP			1
#define LDC_CHANNEL_RESETTING		2

/* ldc_tx_qconf()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_LDC_TX_QCONF
 * ARG0:	channel ID
 * ARG1:	real address base of queue
 * ARG2:	num entries in queue
 * RET0:	status
 *
 * Configure transmit queue for the LDC endpoint specified by the
 * given channel ID, to be placed at the given real address, and
 * be of the given num entries.  Num entries must be a power of two.
 * The real address base of the queue must be aligned on the queue
 * size.  Each queue entry is 64-bytes, so for example, a 32 entry
 * queue must be aligned on a 2048 byte real address boundary.
 *
 * Upon configuration of a valid transmit queue the head and tail
 * pointers are set to a hypervisor specific identical value indicating
 * that the queue initially is empty.
 *
 * The endpoint's transmit queue is un-configured if num entries is zero.
 *
 * The maximum number of entries for each queue for a specific cpu may be
 * determined from the machine description.  A transmit queue may be
 * specified even in the event that the LDC is down (peer endpoint has no
 * receive queue specified).  Transmission will begin as soon as the peer
 * endpoint defines a receive queue.
 *
 * It is recommended that a guest wait for a transmit queue to empty prior
 * to reconfiguring it, or un-configuring it.  Re or un-configuring of a
 * non-empty transmit queue behaves exactly as defined above, however it
 * is undefined as to how many of the pending entries in the original queue
 * will be delivered prior to the re-configuration taking effect.
 * Furthermore, as the queue configuration causes a reset of the head and
 * tail pointers there is no way for a guest to determine how many entries
 * have been sent after the configuration operation.
 */
#define HV_FAST_LDC_TX_QCONF		0xe0

/* ldc_tx_qinfo()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_LDC_TX_QINFO
 * ARG0:	channel ID
 * RET0:	status
 * RET1:	real address base of queue
 * RET2:	num entries in queue
 *
 * Return the configuration info for the transmit queue of LDC endpoint
 * defined by the given channel ID.  The real address is the currently
 * defined real address base of the defined queue, and num entries is the
 * size of the queue in terms of number of entries.
 *
 * If the specified channel ID is a valid endpoint number, but no transmit
 * queue has been defined this service will return success, but with num
 * entries set to zero and the real address will have an undefined value.
 */
#define HV_FAST_LDC_TX_QINFO		0xe1

/* ldc_tx_get_state()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_LDC_TX_GET_STATE
 * ARG0:	channel ID
 * RET0:	status
 * RET1:	head offset
 * RET2:	tail offset
 * RET3:	channel state
 *
 * Return the transmit state, and the head and tail queue pointers, for
 * the transmit queue of the LDC endpoint defined by the given channel ID.
 * The head and tail values are the byte offset of the head and tail
 * positions of the transmit queue for the specified endpoint.
 */
#define HV_FAST_LDC_TX_GET_STATE	0xe2

/* ldc_tx_set_qtail()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_LDC_TX_SET_QTAIL
 * ARG0:	channel ID
 * ARG1:	tail offset
 * RET0:	status
 *
 * Update the tail pointer for the transmit queue associated with the LDC
 * endpoint defined by the given channel ID.  The tail offset specified
 * must be aligned on a 64 byte boundary, and calculated so as to increase
 * the number of pending entries on the transmit queue.  Any attempt to
 * decrease the number of pending transmit queue entires is considered
 * an invalid tail offset and will result in an EINVAL error.
 *
 * Since the tail of the transmit queue may not be moved backwards, the
 * transmit queue may be flushed by configuring a new transmit queue,
 * whereupon the hypervisor will configure the initial transmit head and
 * tail pointers to be equal.
 */
#define HV_FAST_LDC_TX_SET_QTAIL	0xe3

/* ldc_rx_qconf()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_LDC_RX_QCONF
 * ARG0:	channel ID
 * ARG1:	real address base of queue
 * ARG2:	num entries in queue
 * RET0:	status
 *
 * Configure receive queue for the LDC endpoint specified by the
 * given channel ID, to be placed at the given real address, and
 * be of the given num entries.  Num entries must be a power of two.
 * The real address base of the queue must be aligned on the queue
 * size.  Each queue entry is 64-bytes, so for example, a 32 entry
 * queue must be aligned on a 2048 byte real address boundary.
 *
 * The endpoint's transmit queue is un-configured if num entries is zero.
 *
 * If a valid receive queue is specified for a local endpoint the LDC is
 * in the up state for the purpose of transmission to this endpoint.
 *
 * The maximum number of entries for each queue for a specific cpu may be
 * determined from the machine description.
 *
 * As receive queue configuration causes a reset of the queue's head and
 * tail pointers there is no way for a gues to determine how many entries
 * have been received between a preceding ldc_get_rx_state() API call
 * and the completion of the configuration operation.  It should be noted
 * that datagram delivery is not guaranteed via domain channels anyway,
 * and therefore any higher protocol should be resilient to datagram
 * loss if necessary.  However, to overcome this specific race potential
 * it is recommended, for example, that a higher level protocol be employed
 * to ensure either retransmission, or ensure that no datagrams are pending
 * on the peer endpoint's transmit queue prior to the configuration process.
 */
#define HV_FAST_LDC_RX_QCONF		0xe4

/* ldc_rx_qinfo()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_LDC_RX_QINFO
 * ARG0:	channel ID
 * RET0:	status
 * RET1:	real address base of queue
 * RET2:	num entries in queue
 *
 * Return the configuration info for the receive queue of LDC endpoint
 * defined by the given channel ID.  The real address is the currently
 * defined real address base of the defined queue, and num entries is the
 * size of the queue in terms of number of entries.
 *
 * If the specified channel ID is a valid endpoint number, but no receive
 * queue has been defined this service will return success, but with num
 * entries set to zero and the real address will have an undefined value.
 */
#define HV_FAST_LDC_RX_QINFO		0xe5

/* ldc_rx_get_state()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_LDC_RX_GET_STATE
 * ARG0:	channel ID
 * RET0:	status
 * RET1:	head offset
 * RET2:	tail offset
 * RET3:	channel state
 *
 * Return the receive state, and the head and tail queue pointers, for
 * the receive queue of the LDC endpoint defined by the given channel ID.
 * The head and tail values are the byte offset of the head and tail
 * positions of the receive queue for the specified endpoint.
 */
#define HV_FAST_LDC_RX_GET_STATE	0xe6

/* ldc_rx_set_qhead()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_LDC_RX_SET_QHEAD
 * ARG0:	channel ID
 * ARG1:	head offset
 * RET0:	status
 *
 * Update the head pointer for the receive queue associated with the LDC
 * endpoint defined by the given channel ID.  The head offset specified
 * must be aligned on a 64 byte boundary, and calculated so as to decrease
 * the number of pending entries on the receive queue.  Any attempt to
 * increase the number of pending receive queue entires is considered
 * an invalid head offset and will result in an EINVAL error.
 *
 * The receive queue may be flushed by setting the head offset equal
 * to the current tail offset.
 */
#define HV_FAST_LDC_RX_SET_QHEAD	0xe7

/* LDC Map Table Entry.  Each slot is defined by a translation table
 * entry, as specified by the LDC_MTE_* bits below, and a 64-bit
 * hypervisor invalidation cookie.
 */
#define LDC_MTE_PADDR	0x0fffffffffffe000 /* pa[55:13]          */
#define LDC_MTE_COPY_W	0x0000000000000400 /* copy write access  */
#define LDC_MTE_COPY_R	0x0000000000000200 /* copy read access   */
#define LDC_MTE_IOMMU_W	0x0000000000000100 /* IOMMU write access */
#define LDC_MTE_IOMMU_R	0x0000000000000080 /* IOMMU read access  */
#define LDC_MTE_EXEC	0x0000000000000040 /* execute            */
#define LDC_MTE_WRITE	0x0000000000000020 /* read               */
#define LDC_MTE_READ	0x0000000000000010 /* write              */
#define LDC_MTE_SZALL	0x000000000000000f /* page size bits     */
#define LDC_MTE_SZ16GB	0x0000000000000007 /* 16GB page          */
#define LDC_MTE_SZ2GB	0x0000000000000006 /* 2GB page           */
#define LDC_MTE_SZ256MB	0x0000000000000005 /* 256MB page         */
#define LDC_MTE_SZ32MB	0x0000000000000004 /* 32MB page          */
#define LDC_MTE_SZ4MB	0x0000000000000003 /* 4MB page           */
#define LDC_MTE_SZ512K	0x0000000000000002 /* 512K page          */
#define LDC_MTE_SZ64K	0x0000000000000001 /* 64K page           */
#define LDC_MTE_SZ8K	0x0000000000000000 /* 8K page            */

#ifndef __ASSEMBLY__
struct ldc_mtable_entry {
	unsigned long	mte;
	unsigned long	cookie;
};
#endif

/* ldc_set_map_table()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_LDC_SET_MAP_TABLE
 * ARG0:	channel ID
 * ARG1:	table real address
 * ARG2:	num entries
 * RET0:	status
 *
 * Register the MTE table at the given table real address, with the
 * specified num entries, for the LDC indicated by the given channel
 * ID.
 */
#define HV_FAST_LDC_SET_MAP_TABLE	0xea

/* ldc_get_map_table()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_LDC_GET_MAP_TABLE
 * ARG0:	channel ID
 * RET0:	status
 * RET1:	table real address
 * RET2:	num entries
 *
 * Return the configuration of the current mapping table registered
 * for the given channel ID.
 */
#define HV_FAST_LDC_GET_MAP_TABLE	0xeb

#define LDC_COPY_IN	0
#define LDC_COPY_OUT	1

/* ldc_copy()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_LDC_COPY
 * ARG0:	channel ID
 * ARG1:	LDC_COPY_* direction code
 * ARG2:	target real address
 * ARG3:	local real address
 * ARG4:	length in bytes
 * RET0:	status
 * RET1:	actual length in bytes
 */
#define HV_FAST_LDC_COPY		0xec

#define LDC_MEM_READ	1
#define LDC_MEM_WRITE	2
#define LDC_MEM_EXEC	4

/* ldc_mapin()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_LDC_MAPIN
 * ARG0:	channel ID
 * ARG1:	cookie
 * RET0:	status
 * RET1:	real address
 * RET2:	LDC_MEM_* permissions
 */
#define HV_FAST_LDC_MAPIN		0xed

/* ldc_unmap()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_LDC_UNMAP
 * ARG0:	real address
 * RET0:	status
 */
#define HV_FAST_LDC_UNMAP		0xee

/* ldc_revoke()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_LDC_REVOKE
 * ARG0:	channel ID
 * ARG1:	cookie
 * ARG2:	ldc_mtable_entry cookie
 * RET0:	status
 */
#define HV_FAST_LDC_REVOKE		0xef

#ifndef __ASSEMBLY__
unsigned long sun4v_ldc_tx_qconf(unsigned long channel,
				 unsigned long ra,
				 unsigned long num_entries);
unsigned long sun4v_ldc_tx_qinfo(unsigned long channel,
				 unsigned long *ra,
				 unsigned long *num_entries);
unsigned long sun4v_ldc_tx_get_state(unsigned long channel,
				     unsigned long *head_off,
				     unsigned long *tail_off,
				     unsigned long *chan_state);
unsigned long sun4v_ldc_tx_set_qtail(unsigned long channel,
				     unsigned long tail_off);
unsigned long sun4v_ldc_rx_qconf(unsigned long channel,
				 unsigned long ra,
				 unsigned long num_entries);
unsigned long sun4v_ldc_rx_qinfo(unsigned long channel,
				 unsigned long *ra,
				 unsigned long *num_entries);
unsigned long sun4v_ldc_rx_get_state(unsigned long channel,
				     unsigned long *head_off,
				     unsigned long *tail_off,
				     unsigned long *chan_state);
unsigned long sun4v_ldc_rx_set_qhead(unsigned long channel,
				     unsigned long head_off);
unsigned long sun4v_ldc_set_map_table(unsigned long channel,
				      unsigned long ra,
				      unsigned long num_entries);
unsigned long sun4v_ldc_get_map_table(unsigned long channel,
				      unsigned long *ra,
				      unsigned long *num_entries);
unsigned long sun4v_ldc_copy(unsigned long channel,
			     unsigned long dir_code,
			     unsigned long tgt_raddr,
			     unsigned long lcl_raddr,
			     unsigned long len,
			     unsigned long *actual_len);
unsigned long sun4v_ldc_mapin(unsigned long channel,
			      unsigned long cookie,
			      unsigned long *ra,
			      unsigned long *perm);
unsigned long sun4v_ldc_unmap(unsigned long ra);
unsigned long sun4v_ldc_revoke(unsigned long channel,
			       unsigned long cookie,
			       unsigned long mte_cookie);
#endif

/* Performance counter services.  */

#define HV_PERF_JBUS_PERF_CTRL_REG	0x00
#define HV_PERF_JBUS_PERF_CNT_REG	0x01
#define HV_PERF_DRAM_PERF_CTRL_REG_0	0x02
#define HV_PERF_DRAM_PERF_CNT_REG_0	0x03
#define HV_PERF_DRAM_PERF_CTRL_REG_1	0x04
#define HV_PERF_DRAM_PERF_CNT_REG_1	0x05
#define HV_PERF_DRAM_PERF_CTRL_REG_2	0x06
#define HV_PERF_DRAM_PERF_CNT_REG_2	0x07
#define HV_PERF_DRAM_PERF_CTRL_REG_3	0x08
#define HV_PERF_DRAM_PERF_CNT_REG_3	0x09

/* get_perfreg()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_GET_PERFREG
 * ARG0:	performance reg number
 * RET0:	status
 * RET1:	performance reg value
 * ERRORS:	EINVAL		Invalid performance register number
 *		ENOACCESS	No access allowed to performance counters
 *
 * Read the value of the given DRAM/JBUS performance counter/control register.
 */
#define HV_FAST_GET_PERFREG		0x100

/* set_perfreg()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_SET_PERFREG
 * ARG0:	performance reg number
 * ARG1:	performance reg value
 * RET0:	status
 * ERRORS:	EINVAL		Invalid performance register number
 *		ENOACCESS	No access allowed to performance counters
 *
 * Write the given performance reg value to the given DRAM/JBUS
 * performance counter/control register.
 */
#define HV_FAST_SET_PERFREG		0x101

#define HV_N2_PERF_SPARC_CTL		0x0
#define HV_N2_PERF_DRAM_CTL0		0x1
#define HV_N2_PERF_DRAM_CNT0		0x2
#define HV_N2_PERF_DRAM_CTL1		0x3
#define HV_N2_PERF_DRAM_CNT1		0x4
#define HV_N2_PERF_DRAM_CTL2		0x5
#define HV_N2_PERF_DRAM_CNT2		0x6
#define HV_N2_PERF_DRAM_CTL3		0x7
#define HV_N2_PERF_DRAM_CNT3		0x8

#define HV_FAST_N2_GET_PERFREG		0x104
#define HV_FAST_N2_SET_PERFREG		0x105

#ifndef __ASSEMBLY__
unsigned long sun4v_niagara_getperf(unsigned long reg,
				    unsigned long *val);
unsigned long sun4v_niagara_setperf(unsigned long reg,
				    unsigned long val);
unsigned long sun4v_niagara2_getperf(unsigned long reg,
				     unsigned long *val);
unsigned long sun4v_niagara2_setperf(unsigned long reg,
				     unsigned long val);
#endif

/* MMU statistics services.
 *
 * The hypervisor maintains MMU statistics and privileged code provides
 * a buffer where these statistics can be collected.  It is continually
 * updated once configured.  The layout is as follows:
 */
#ifndef __ASSEMBLY__
struct hv_mmu_statistics {
	unsigned long immu_tsb_hits_ctx0_8k_tte;
	unsigned long immu_tsb_ticks_ctx0_8k_tte;
	unsigned long immu_tsb_hits_ctx0_64k_tte;
	unsigned long immu_tsb_ticks_ctx0_64k_tte;
	unsigned long __reserved1[2];
	unsigned long immu_tsb_hits_ctx0_4mb_tte;
	unsigned long immu_tsb_ticks_ctx0_4mb_tte;
	unsigned long __reserved2[2];
	unsigned long immu_tsb_hits_ctx0_256mb_tte;
	unsigned long immu_tsb_ticks_ctx0_256mb_tte;
	unsigned long __reserved3[4];
	unsigned long immu_tsb_hits_ctxnon0_8k_tte;
	unsigned long immu_tsb_ticks_ctxnon0_8k_tte;
	unsigned long immu_tsb_hits_ctxnon0_64k_tte;
	unsigned long immu_tsb_ticks_ctxnon0_64k_tte;
	unsigned long __reserved4[2];
	unsigned long immu_tsb_hits_ctxnon0_4mb_tte;
	unsigned long immu_tsb_ticks_ctxnon0_4mb_tte;
	unsigned long __reserved5[2];
	unsigned long immu_tsb_hits_ctxnon0_256mb_tte;
	unsigned long immu_tsb_ticks_ctxnon0_256mb_tte;
	unsigned long __reserved6[4];
	unsigned long dmmu_tsb_hits_ctx0_8k_tte;
	unsigned long dmmu_tsb_ticks_ctx0_8k_tte;
	unsigned long dmmu_tsb_hits_ctx0_64k_tte;
	unsigned long dmmu_tsb_ticks_ctx0_64k_tte;
	unsigned long __reserved7[2];
	unsigned long dmmu_tsb_hits_ctx0_4mb_tte;
	unsigned long dmmu_tsb_ticks_ctx0_4mb_tte;
	unsigned long __reserved8[2];
	unsigned long dmmu_tsb_hits_ctx0_256mb_tte;
	unsigned long dmmu_tsb_ticks_ctx0_256mb_tte;
	unsigned long __reserved9[4];
	unsigned long dmmu_tsb_hits_ctxnon0_8k_tte;
	unsigned long dmmu_tsb_ticks_ctxnon0_8k_tte;
	unsigned long dmmu_tsb_hits_ctxnon0_64k_tte;
	unsigned long dmmu_tsb_ticks_ctxnon0_64k_tte;
	unsigned long __reserved10[2];
	unsigned long dmmu_tsb_hits_ctxnon0_4mb_tte;
	unsigned long dmmu_tsb_ticks_ctxnon0_4mb_tte;
	unsigned long __reserved11[2];
	unsigned long dmmu_tsb_hits_ctxnon0_256mb_tte;
	unsigned long dmmu_tsb_ticks_ctxnon0_256mb_tte;
	unsigned long __reserved12[4];
};
#endif

/* mmustat_conf()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_MMUSTAT_CONF
 * ARG0:	real address
 * RET0:	status
 * RET1:	real address
 * ERRORS:	ENORADDR	Invalid real address
 *		EBADALIGN	Real address not aligned on 64-byte boundary
 *		EBADTRAP	API not supported on this processor
 *
 * Enable MMU statistic gathering using the buffer at the given real
 * address on the current virtual CPU.  The new buffer real address
 * is given in ARG1, and the previously specified buffer real address
 * is returned in RET1, or is returned as zero for the first invocation.
 *
 * If the passed in real address argument is zero, this will disable
 * MMU statistic collection on the current virtual CPU.  If an error is
 * returned then no statistics are collected.
 *
 * The buffer contents should be initialized to all zeros before being
 * given to the hypervisor or else the statistics will be meaningless.
 */
#define HV_FAST_MMUSTAT_CONF		0x102

/* mmustat_info()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_MMUSTAT_INFO
 * RET0:	status
 * RET1:	real address
 * ERRORS:	EBADTRAP	API not supported on this processor
 *
 * Return the current state and real address of the currently configured
 * MMU statistics buffer on the current virtual CPU.
 */
#define HV_FAST_MMUSTAT_INFO		0x103

#ifndef __ASSEMBLY__
unsigned long sun4v_mmustat_conf(unsigned long ra, unsigned long *orig_ra);
unsigned long sun4v_mmustat_info(unsigned long *ra);
#endif

/* NCS crypto services  */

/* ncs_request() sub-function numbers */
#define HV_NCS_QCONF			0x01
#define HV_NCS_QTAIL_UPDATE		0x02

#ifndef __ASSEMBLY__
struct hv_ncs_queue_entry {
	/* MAU Control Register */
	unsigned long	mau_control;
#define MAU_CONTROL_INV_PARITY	0x0000000000002000
#define MAU_CONTROL_STRAND	0x0000000000001800
#define MAU_CONTROL_BUSY	0x0000000000000400
#define MAU_CONTROL_INT		0x0000000000000200
#define MAU_CONTROL_OP		0x00000000000001c0
#define MAU_CONTROL_OP_SHIFT	6
#define MAU_OP_LOAD_MA_MEMORY	0x0
#define MAU_OP_STORE_MA_MEMORY	0x1
#define MAU_OP_MODULAR_MULT	0x2
#define MAU_OP_MODULAR_REDUCE	0x3
#define MAU_OP_MODULAR_EXP_LOOP	0x4
#define MAU_CONTROL_LEN		0x000000000000003f
#define MAU_CONTROL_LEN_SHIFT	0

	/* Real address of bytes to load or store bytes
	 * into/out-of the MAU.
	 */
	unsigned long	mau_mpa;

	/* Modular Arithmetic MA Offset Register.  */
	unsigned long	mau_ma;

	/* Modular Arithmetic N Prime Register.  */
	unsigned long	mau_np;
};

struct hv_ncs_qconf_arg {
	unsigned long	mid;      /* MAU ID, 1 per core on Niagara */
	unsigned long	base;     /* Real address base of queue */
	unsigned long	end;	  /* Real address end of queue */
	unsigned long	num_ents; /* Number of entries in queue */
};

struct hv_ncs_qtail_update_arg {
	unsigned long	mid;      /* MAU ID, 1 per core on Niagara */
	unsigned long	tail;     /* New tail index to use */
	unsigned long	syncflag; /* only SYNCFLAG_SYNC is implemented */
#define HV_NCS_SYNCFLAG_SYNC	0x00
#define HV_NCS_SYNCFLAG_ASYNC	0x01
};
#endif

/* ncs_request()
 * TRAP:	HV_FAST_TRAP
 * FUNCTION:	HV_FAST_NCS_REQUEST
 * ARG0:	NCS sub-function
 * ARG1:	sub-function argument real address
 * ARG2:	size in bytes of sub-function argument
 * RET0:	status
 *
 * The MAU chip of the Niagara processor is not directly accessible
 * to privileged code, instead it is programmed indirectly via this
 * hypervisor API.
 *
 * The interfaces defines a queue of MAU operations to perform.
 * Privileged code registers a queue with the hypervisor by invoking
 * this HVAPI with the HV_NCS_QCONF sub-function, which defines the
 * base, end, and number of entries of the queue.  Each queue entry
 * contains a MAU register struct block.
 *
 * The privileged code then proceeds to add entries to the queue and
 * then invoke the HV_NCS_QTAIL_UPDATE sub-function.  Since only
 * synchronous operations are supported by the current hypervisor,
 * HV_NCS_QTAIL_UPDATE will run all the pending queue entries to
 * completion and return HV_EOK, or return an error code.
 *
 * The real address of the sub-function argument must be aligned on at
 * least an 8-byte boundary.
 *
 * The tail argument of HV_NCS_QTAIL_UPDATE is an index, not a byte
 * offset, into the queue and must be less than or equal the 'num_ents'
 * argument given in the HV_NCS_QCONF call.
 */
#define HV_FAST_NCS_REQUEST		0x110

#ifndef __ASSEMBLY__
unsigned long sun4v_ncs_request(unsigned long request,
			        unsigned long arg_ra,
			        unsigned long arg_size);
#endif

#define HV_FAST_FIRE_GET_PERFREG	0x120
#define HV_FAST_FIRE_SET_PERFREG	0x121

#define HV_FAST_REBOOT_DATA_SET		0x172

#ifndef __ASSEMBLY__
unsigned long sun4v_reboot_data_set(unsigned long ra,
				    unsigned long len);
#endif

#define HV_FAST_VT_GET_PERFREG		0x184
#define HV_FAST_VT_SET_PERFREG		0x185

#ifndef __ASSEMBLY__
unsigned long sun4v_vt_get_perfreg(unsigned long reg_num,
				   unsigned long *reg_val);
unsigned long sun4v_vt_set_perfreg(unsigned long reg_num,
				   unsigned long reg_val);
#endif

#define	HV_FAST_T5_GET_PERFREG		0x1a8
#define	HV_FAST_T5_SET_PERFREG		0x1a9

#ifndef	__ASSEMBLY__
unsigned long sun4v_t5_get_perfreg(unsigned long reg_num,
				   unsigned long *reg_val);
unsigned long sun4v_t5_set_perfreg(unsigned long reg_num,
				   unsigned long reg_val);
#endif


#define HV_FAST_M7_GET_PERFREG	0x43
#define HV_FAST_M7_SET_PERFREG	0x44

#ifndef	__ASSEMBLY__
unsigned long sun4v_m7_get_perfreg(unsigned long reg_num,
				      unsigned long *reg_val);
unsigned long sun4v_m7_set_perfreg(unsigned long reg_num,
				      unsigned long reg_val);
#endif

/* Function numbers for HV_CORE_TRAP.  */
#define HV_CORE_SET_VER			0x00
#define HV_CORE_PUTCHAR			0x01
#define HV_CORE_EXIT			0x02
#define HV_CORE_GET_VER			0x03

/* Hypervisor API groups for use with HV_CORE_SET_VER and
 * HV_CORE_GET_VER.
 */
#define HV_GRP_SUN4V			0x0000
#define HV_GRP_CORE			0x0001
#define HV_GRP_INTR			0x0002
#define HV_GRP_SOFT_STATE		0x0003
#define HV_GRP_TM			0x0080
#define HV_GRP_PCI			0x0100
#define HV_GRP_LDOM			0x0101
#define HV_GRP_SVC_CHAN			0x0102
#define HV_GRP_NCS			0x0103
#define HV_GRP_RNG			0x0104
#define HV_GRP_PBOOT			0x0105
#define HV_GRP_TPM			0x0107
#define HV_GRP_SDIO			0x0108
#define HV_GRP_SDIO_ERR			0x0109
#define HV_GRP_REBOOT_DATA		0x0110
#define HV_GRP_ATU			0x0111
#define HV_GRP_DAX			0x0113
#define HV_GRP_M7_PERF			0x0114
#define HV_GRP_NIAG_PERF		0x0200
#define HV_GRP_FIRE_PERF		0x0201
#define HV_GRP_N2_CPU			0x0202
#define HV_GRP_NIU			0x0204
#define HV_GRP_VF_CPU			0x0205
#define HV_GRP_KT_CPU			0x0209
#define HV_GRP_VT_CPU			0x020c
#define HV_GRP_T5_CPU			0x0211
#define HV_GRP_DIAG			0x0300

#ifndef __ASSEMBLY__
unsigned long sun4v_get_version(unsigned long group,
			        unsigned long *major,
			        unsigned long *minor);
unsigned long sun4v_set_version(unsigned long group,
			        unsigned long major,
			        unsigned long minor,
			        unsigned long *actual_minor);

int sun4v_hvapi_register(unsigned long group, unsigned long major,
			 unsigned long *minor);
void sun4v_hvapi_unregister(unsigned long group);
int sun4v_hvapi_get(unsigned long group,
		    unsigned long *major,
		    unsigned long *minor);
void sun4v_hvapi_init(void);
#endif

#endif /* !(_SPARC64_HYPERVISOR_H) */
