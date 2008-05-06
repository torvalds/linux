	/* %o0: devhandle
	 * %o1:	devino
	 *
	 * returns %o0: sysino
	 */
	.globl	sun4v_devino_to_sysino
	.type	sun4v_devino_to_sysino,#function
sun4v_devino_to_sysino:
	mov	HV_FAST_INTR_DEVINO2SYSINO, %o5
	ta	HV_FAST_TRAP
	retl
	 mov	%o1, %o0
	.size	sun4v_devino_to_sysino, .-sun4v_devino_to_sysino

	/* %o0: sysino
	 *
	 * returns %o0: intr_enabled (HV_INTR_{DISABLED,ENABLED})
	 */
	.globl	sun4v_intr_getenabled
	.type	sun4v_intr_getenabled,#function
sun4v_intr_getenabled:
	mov	HV_FAST_INTR_GETENABLED, %o5
	ta	HV_FAST_TRAP
	retl
	 mov	%o1, %o0
	.size	sun4v_intr_getenabled, .-sun4v_intr_getenabled

	/* %o0: sysino
	 * %o1: intr_enabled (HV_INTR_{DISABLED,ENABLED})
	 */
	.globl	sun4v_intr_setenabled
	.type	sun4v_intr_setenabled,#function
sun4v_intr_setenabled:
	mov	HV_FAST_INTR_SETENABLED, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
	.size	sun4v_intr_setenabled, .-sun4v_intr_setenabled

	/* %o0: sysino
	 *
	 * returns %o0: intr_state (HV_INTR_STATE_*)
	 */
	.globl	sun4v_intr_getstate
	.type	sun4v_intr_getstate,#function
sun4v_intr_getstate:
	mov	HV_FAST_INTR_GETSTATE, %o5
	ta	HV_FAST_TRAP
	retl
	 mov	%o1, %o0
	.size	sun4v_intr_getstate, .-sun4v_intr_getstate

	/* %o0: sysino
	 * %o1: intr_state (HV_INTR_STATE_*)
	 */
	.globl	sun4v_intr_setstate
	.type	sun4v_intr_setstate,#function
sun4v_intr_setstate:
	mov	HV_FAST_INTR_SETSTATE, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
	.size	sun4v_intr_setstate, .-sun4v_intr_setstate

	/* %o0: sysino
	 *
	 * returns %o0: cpuid
	 */
	.globl	sun4v_intr_gettarget
	.type	sun4v_intr_gettarget,#function
sun4v_intr_gettarget:
	mov	HV_FAST_INTR_GETTARGET, %o5
	ta	HV_FAST_TRAP
	retl
	 mov	%o1, %o0
	.size	sun4v_intr_gettarget, .-sun4v_intr_gettarget

	/* %o0: sysino
	 * %o1: cpuid
	 */
	.globl	sun4v_intr_settarget
	.type	sun4v_intr_settarget,#function
sun4v_intr_settarget:
	mov	HV_FAST_INTR_SETTARGET, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
	.size	sun4v_intr_settarget, .-sun4v_intr_settarget

	/* %o0:	cpuid
	 * %o1: pc
	 * %o2:	rtba
	 * %o3:	arg0
	 *
	 * returns %o0:	status
	 */
	.globl	sun4v_cpu_start
	.type	sun4v_cpu_start,#function
sun4v_cpu_start:
	mov	HV_FAST_CPU_START, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
	.size	sun4v_cpu_start, .-sun4v_cpu_start

	/* %o0:	cpuid
	 *
	 * returns %o0: status
	 */
	.globl	sun4v_cpu_stop
	.type	sun4v_cpu_stop,#function
sun4v_cpu_stop:
	mov	HV_FAST_CPU_STOP, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
	.size	sun4v_cpu_stop, .-sun4v_cpu_stop

	/* returns %o0:	status  */
	.globl	sun4v_cpu_yield
	.type	sun4v_cpu_yield, #function
sun4v_cpu_yield:
	mov	HV_FAST_CPU_YIELD, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
	.size	sun4v_cpu_yield, .-sun4v_cpu_yield

	/* %o0:	type
	 * %o1:	queue paddr
	 * %o2:	num queue entries
	 *
	 * returns %o0:	status
	 */
	.globl	sun4v_cpu_qconf
	.type	sun4v_cpu_qconf,#function
sun4v_cpu_qconf:
	mov	HV_FAST_CPU_QCONF, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
	.size	sun4v_cpu_qconf, .-sun4v_cpu_qconf

	/* %o0:	num cpus in cpu list
	 * %o1:	cpu list paddr
	 * %o2:	mondo block paddr
	 *
	 * returns %o0: status
	 */
	.globl	sun4v_cpu_mondo_send
	.type	sun4v_cpu_mondo_send,#function
sun4v_cpu_mondo_send:
	mov	HV_FAST_CPU_MONDO_SEND, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
	.size	sun4v_cpu_mondo_send, .-sun4v_cpu_mondo_send

	/* %o0:	CPU ID
	 *
	 * returns %o0:	-status if status non-zero, else
	 *         %o0:	cpu state as HV_CPU_STATE_*
	 */
	.globl	sun4v_cpu_state
	.type	sun4v_cpu_state,#function
sun4v_cpu_state:
	mov	HV_FAST_CPU_STATE, %o5
	ta	HV_FAST_TRAP
	brnz,pn	%o0, 1f
	 sub	%g0, %o0, %o0
	mov	%o1, %o0
1:	retl
	 nop
	.size	sun4v_cpu_state, .-sun4v_cpu_state

	/* %o0: virtual address
	 * %o1: must be zero
	 * %o2: TTE
	 * %o3: HV_MMU_* flags
	 *
	 * returns %o0: status
	 */
	.globl	sun4v_mmu_map_perm_addr
	.type	sun4v_mmu_map_perm_addr,#function
sun4v_mmu_map_perm_addr:
	mov	HV_FAST_MMU_MAP_PERM_ADDR, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
	.size	sun4v_mmu_map_perm_addr, .-sun4v_mmu_map_perm_addr

	/* %o0: number of TSB descriptions
	 * %o1: TSB descriptions real address
	 *
	 * returns %o0: status
	 */
	.globl	sun4v_mmu_tsb_ctx0
	.type	sun4v_mmu_tsb_ctx0,#function
sun4v_mmu_tsb_ctx0:
	mov	HV_FAST_MMU_TSB_CTX0, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
	.size	sun4v_mmu_tsb_ctx0, .-sun4v_mmu_tsb_ctx0

	/* %o0:	API group number
	 * %o1: pointer to unsigned long major number storage
	 * %o2: pointer to unsigned long minor number storage
	 *
	 * returns %o0: status
	 */
	.globl	sun4v_get_version
	.type	sun4v_get_version,#function
sun4v_get_version:
	mov	HV_CORE_GET_VER, %o5
	mov	%o1, %o3
	mov	%o2, %o4
	ta	HV_CORE_TRAP
	stx	%o1, [%o3]
	retl
	 stx	%o2, [%o4]
	.size	sun4v_get_version, .-sun4v_get_version

	/* %o0: API group number
	 * %o1: desired major number
	 * %o2: desired minor number
	 * %o3: pointer to unsigned long actual minor number storage
	 *
	 * returns %o0: status
	 */
	.globl	sun4v_set_version
	.type	sun4v_set_version,#function
sun4v_set_version:
	mov	HV_CORE_SET_VER, %o5
	mov	%o3, %o4
	ta	HV_CORE_TRAP
	retl
	 stx	%o1, [%o4]
	.size	sun4v_set_version, .-sun4v_set_version

	/* %o0: pointer to unsigned long time
	 *
	 * returns %o0: status
	 */
	.globl	sun4v_tod_get
	.type	sun4v_tod_get,#function
sun4v_tod_get:
	mov	%o0, %o4
	mov	HV_FAST_TOD_GET, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%o4]
	retl
	 nop
	.size	sun4v_tod_get, .-sun4v_tod_get

	/* %o0: time
	 *
	 * returns %o0: status
	 */
	.globl	sun4v_tod_set
	.type	sun4v_tod_set,#function
sun4v_tod_set:
	mov	HV_FAST_TOD_SET, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
	.size	sun4v_tod_set, .-sun4v_tod_set

	/* %o0: pointer to unsigned long status
	 *
	 * returns %o0: signed character
	 */
	.globl	sun4v_con_getchar
	.type	sun4v_con_getchar,#function
sun4v_con_getchar:
	mov	%o0, %o4
	mov	HV_FAST_CONS_GETCHAR, %o5
	clr	%o0
	clr	%o1
	ta	HV_FAST_TRAP
	stx	%o0, [%o4]
	retl
	 sra	%o1, 0, %o0
	.size	sun4v_con_getchar, .-sun4v_con_getchar

	/* %o0: signed long character
	 *
	 * returns %o0: status
	 */
	.globl	sun4v_con_putchar
	.type	sun4v_con_putchar,#function
sun4v_con_putchar:
	mov	HV_FAST_CONS_PUTCHAR, %o5
	ta	HV_FAST_TRAP
	retl
	 sra	%o0, 0, %o0
	.size	sun4v_con_putchar, .-sun4v_con_putchar

	/* %o0: buffer real address
	 * %o1: buffer size
	 * %o2: pointer to unsigned long bytes_read
	 *
	 * returns %o0: status
	 */
	.globl	sun4v_con_read
	.type	sun4v_con_read,#function
sun4v_con_read:
	mov	%o2, %o4
	mov	HV_FAST_CONS_READ, %o5
	ta	HV_FAST_TRAP
	brnz	%o0, 1f
	 cmp	%o1, -1		/* break */
	be,a,pn	%icc, 1f
	 mov	%o1, %o0
	cmp	%o1, -2		/* hup */
	be,a,pn	%icc, 1f
	 mov	%o1, %o0
	stx	%o1, [%o4]
1:	retl
	 nop
	.size	sun4v_con_read, .-sun4v_con_read

	/* %o0: buffer real address
	 * %o1: buffer size
	 * %o2: pointer to unsigned long bytes_written
	 *
	 * returns %o0: status
	 */
	.globl	sun4v_con_write
	.type	sun4v_con_write,#function
sun4v_con_write:
	mov	%o2, %o4
	mov	HV_FAST_CONS_WRITE, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%o4]
	retl
	 nop
	.size	sun4v_con_write, .-sun4v_con_write

	/* %o0:	soft state
	 * %o1:	address of description string
	 *
	 * returns %o0: status
	 */
	.globl	sun4v_mach_set_soft_state
	.type	sun4v_mach_set_soft_state,#function
sun4v_mach_set_soft_state:
	mov	HV_FAST_MACH_SET_SOFT_STATE, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
	.size	sun4v_mach_set_soft_state, .-sun4v_mach_set_soft_state

	/* %o0: exit code
	 *
	 * Does not return.
	 */
	.globl	sun4v_mach_exit
	.type	sun4v_mach_exit,#function
sun4v_mach_exit:
	mov	HV_FAST_MACH_EXIT, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
	.size	sun4v_mach_exit, .-sun4v_mach_exit

	/* %o0: buffer real address
	 * %o1: buffer length
	 * %o2: pointer to unsigned long real_buf_len
	 *
	 * returns %o0: status
	 */
	.globl	sun4v_mach_desc
	.type	sun4v_mach_desc,#function
sun4v_mach_desc:
	mov	%o2, %o4
	mov	HV_FAST_MACH_DESC, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%o4]
	retl
	 nop
	.size	sun4v_mach_desc, .-sun4v_mach_desc

	/* %o0: new timeout in milliseconds
	 * %o1: pointer to unsigned long orig_timeout
	 *
	 * returns %o0: status
	 */
	.globl	sun4v_mach_set_watchdog
	.type	sun4v_mach_set_watchdog,#function
sun4v_mach_set_watchdog:
	mov	%o1, %o4
	mov	HV_FAST_MACH_SET_WATCHDOG, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%o4]
	retl
	 nop
	.size	sun4v_mach_set_watchdog, .-sun4v_mach_set_watchdog

	/* No inputs and does not return.  */
	.globl	sun4v_mach_sir
	.type	sun4v_mach_sir,#function
sun4v_mach_sir:
	mov	%o1, %o4
	mov	HV_FAST_MACH_SIR, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%o4]
	retl
	 nop
	.size	sun4v_mach_sir, .-sun4v_mach_sir

	/* %o0: channel
	 * %o1:	ra
	 * %o2:	num_entries
	 *
	 * returns %o0:	status
	 */
	.globl	sun4v_ldc_tx_qconf
	.type	sun4v_ldc_tx_qconf,#function
sun4v_ldc_tx_qconf:
	mov	HV_FAST_LDC_TX_QCONF, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
	.size	sun4v_ldc_tx_qconf, .-sun4v_ldc_tx_qconf

	/* %o0: channel
	 * %o1:	pointer to unsigned long ra
	 * %o2:	pointer to unsigned long num_entries
	 *
	 * returns %o0:	status
	 */
	.globl	sun4v_ldc_tx_qinfo
	.type	sun4v_ldc_tx_qinfo,#function
sun4v_ldc_tx_qinfo:
	mov	%o1, %g1
	mov	%o2, %g2
	mov	HV_FAST_LDC_TX_QINFO, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%g1]
	stx	%o2, [%g2]
	retl
	 nop
	.size	sun4v_ldc_tx_qinfo, .-sun4v_ldc_tx_qinfo

	/* %o0: channel
	 * %o1:	pointer to unsigned long head_off
	 * %o2:	pointer to unsigned long tail_off
	 * %o2:	pointer to unsigned long chan_state
	 *
	 * returns %o0:	status
	 */
	.globl	sun4v_ldc_tx_get_state
	.type	sun4v_ldc_tx_get_state,#function
sun4v_ldc_tx_get_state:
	mov	%o1, %g1
	mov	%o2, %g2
	mov	%o3, %g3
	mov	HV_FAST_LDC_TX_GET_STATE, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%g1]
	stx	%o2, [%g2]
	stx	%o3, [%g3]
	retl
	 nop
	.size	sun4v_ldc_tx_get_state, .-sun4v_ldc_tx_get_state

	/* %o0: channel
	 * %o1:	tail_off
	 *
	 * returns %o0:	status
	 */
	.globl	sun4v_ldc_tx_set_qtail
	.type	sun4v_ldc_tx_set_qtail,#function
sun4v_ldc_tx_set_qtail:
	mov	HV_FAST_LDC_TX_SET_QTAIL, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
	.size	sun4v_ldc_tx_set_qtail, .-sun4v_ldc_tx_set_qtail

	/* %o0: channel
	 * %o1:	ra
	 * %o2:	num_entries
	 *
	 * returns %o0:	status
	 */
	.globl	sun4v_ldc_rx_qconf
	.type	sun4v_ldc_rx_qconf,#function
sun4v_ldc_rx_qconf:
	mov	HV_FAST_LDC_RX_QCONF, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
	.size	sun4v_ldc_rx_qconf, .-sun4v_ldc_rx_qconf

	/* %o0: channel
	 * %o1:	pointer to unsigned long ra
	 * %o2:	pointer to unsigned long num_entries
	 *
	 * returns %o0:	status
	 */
	.globl	sun4v_ldc_rx_qinfo
	.type	sun4v_ldc_rx_qinfo,#function
sun4v_ldc_rx_qinfo:
	mov	%o1, %g1
	mov	%o2, %g2
	mov	HV_FAST_LDC_RX_QINFO, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%g1]
	stx	%o2, [%g2]
	retl
	 nop
	.size	sun4v_ldc_rx_qinfo, .-sun4v_ldc_rx_qinfo

	/* %o0: channel
	 * %o1:	pointer to unsigned long head_off
	 * %o2:	pointer to unsigned long tail_off
	 * %o2:	pointer to unsigned long chan_state
	 *
	 * returns %o0:	status
	 */
	.globl	sun4v_ldc_rx_get_state
	.type	sun4v_ldc_rx_get_state,#function
sun4v_ldc_rx_get_state:
	mov	%o1, %g1
	mov	%o2, %g2
	mov	%o3, %g3
	mov	HV_FAST_LDC_RX_GET_STATE, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%g1]
	stx	%o2, [%g2]
	stx	%o3, [%g3]
	retl
	 nop
	.size	sun4v_ldc_rx_get_state, .-sun4v_ldc_rx_get_state

	/* %o0: channel
	 * %o1:	head_off
	 *
	 * returns %o0:	status
	 */
	.globl	sun4v_ldc_rx_set_qhead
	.type	sun4v_ldc_rx_set_qhead,#function
sun4v_ldc_rx_set_qhead:
	mov	HV_FAST_LDC_RX_SET_QHEAD, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
	.size	sun4v_ldc_rx_set_qhead, .-sun4v_ldc_rx_set_qhead

	/* %o0: channel
	 * %o1:	ra
	 * %o2:	num_entries
	 *
	 * returns %o0: status
	 */
	.globl	sun4v_ldc_set_map_table
	.type	sun4v_ldc_set_map_table,#function
sun4v_ldc_set_map_table:
	mov	HV_FAST_LDC_SET_MAP_TABLE, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
	.size	sun4v_ldc_set_map_table, .-sun4v_ldc_set_map_table

	/* %o0: channel
	 * %o1:	pointer to unsigned long ra
	 * %o2:	pointer to unsigned long num_entries
	 *
	 * returns %o0: status
	 */
	.globl	sun4v_ldc_get_map_table
	.type	sun4v_ldc_get_map_table,#function
sun4v_ldc_get_map_table:
	mov	%o1, %g1
	mov	%o2, %g2
	mov	HV_FAST_LDC_GET_MAP_TABLE, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%g1]
	stx	%o2, [%g2]
	retl
	 nop
	.size	sun4v_ldc_get_map_table, .-sun4v_ldc_get_map_table

	/* %o0:	channel
	 * %o1:	dir_code
	 * %o2:	tgt_raddr
	 * %o3:	lcl_raddr
	 * %o4:	len
	 * %o5:	pointer to unsigned long actual_len
	 *
	 * returns %o0:	status
	 */
	.globl	sun4v_ldc_copy
	.type	sun4v_ldc_copy,#function
sun4v_ldc_copy:
	mov	%o5, %g1
	mov	HV_FAST_LDC_COPY, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%g1]
	retl
	 nop
	.size	sun4v_ldc_copy, .-sun4v_ldc_copy

	/* %o0:	channel
	 * %o1:	cookie
	 * %o2:	pointer to unsigned long ra
	 * %o3:	pointer to unsigned long perm
	 *
	 * returns %o0:	status
	 */
	.globl	sun4v_ldc_mapin
	.type	sun4v_ldc_mapin,#function
sun4v_ldc_mapin:
	mov	%o2, %g1
	mov	%o3, %g2
	mov	HV_FAST_LDC_MAPIN, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%g1]
	stx	%o2, [%g2]
	retl
	 nop
	.size	sun4v_ldc_mapin, .-sun4v_ldc_mapin

	/* %o0:	ra
	 *
	 * returns %o0:	status
	 */
	.globl	sun4v_ldc_unmap
	.type	sun4v_ldc_unmap,#function
sun4v_ldc_unmap:
	mov	HV_FAST_LDC_UNMAP, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
	.size	sun4v_ldc_unmap, .-sun4v_ldc_unmap

	/* %o0: channel
	 * %o1:	cookie
	 * %o2:	mte_cookie
	 *
	 * returns %o0:	status
	 */
	.globl	sun4v_ldc_revoke
	.type	sun4v_ldc_revoke,#function
sun4v_ldc_revoke:
	mov	HV_FAST_LDC_REVOKE, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
	.size	sun4v_ldc_revoke, .-sun4v_ldc_revoke

	/* %o0: device handle
	 * %o1:	device INO
	 * %o2:	pointer to unsigned long cookie
	 *
	 * returns %o0: status
	 */
	.globl	sun4v_vintr_get_cookie
	.type	sun4v_vintr_get_cookie,#function
sun4v_vintr_get_cookie:
	mov	%o2, %g1
	mov	HV_FAST_VINTR_GET_COOKIE, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%g1]
	retl
	 nop
	.size	sun4v_vintr_get_cookie, .-sun4v_vintr_get_cookie

	/* %o0: device handle
	 * %o1:	device INO
	 * %o2:	cookie
	 *
	 * returns %o0: status
	 */
	.globl	sun4v_vintr_set_cookie
	.type	sun4v_vintr_set_cookie,#function
sun4v_vintr_set_cookie:
	mov	HV_FAST_VINTR_SET_COOKIE, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
	.size	sun4v_vintr_set_cookie, .-sun4v_vintr_set_cookie

	/* %o0: device handle
	 * %o1:	device INO
	 * %o2:	pointer to unsigned long valid_state
	 *
	 * returns %o0: status
	 */
	.globl	sun4v_vintr_get_valid
	.type	sun4v_vintr_get_valid,#function
sun4v_vintr_get_valid:
	mov	%o2, %g1
	mov	HV_FAST_VINTR_GET_VALID, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%g1]
	retl
	 nop
	.size	sun4v_vintr_get_valid, .-sun4v_vintr_get_valid

	/* %o0: device handle
	 * %o1:	device INO
	 * %o2:	valid_state
	 *
	 * returns %o0: status
	 */
	.globl	sun4v_vintr_set_valid
	.type	sun4v_vintr_set_valid,#function
sun4v_vintr_set_valid:
	mov	HV_FAST_VINTR_SET_VALID, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
	.size	sun4v_vintr_set_valid, .-sun4v_vintr_set_valid

	/* %o0: device handle
	 * %o1:	device INO
	 * %o2:	pointer to unsigned long state
	 *
	 * returns %o0: status
	 */
	.globl	sun4v_vintr_get_state
	.type	sun4v_vintr_get_state,#function
sun4v_vintr_get_state:
	mov	%o2, %g1
	mov	HV_FAST_VINTR_GET_STATE, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%g1]
	retl
	 nop
	.size	sun4v_vintr_get_state, .-sun4v_vintr_get_state

	/* %o0: device handle
	 * %o1:	device INO
	 * %o2:	state
	 *
	 * returns %o0: status
	 */
	.globl	sun4v_vintr_set_state
	.type	sun4v_vintr_set_state,#function
sun4v_vintr_set_state:
	mov	HV_FAST_VINTR_SET_STATE, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
	.size	sun4v_vintr_set_state, .-sun4v_vintr_set_state

	/* %o0: device handle
	 * %o1:	device INO
	 * %o2:	pointer to unsigned long cpuid
	 *
	 * returns %o0: status
	 */
	.globl	sun4v_vintr_get_target
	.type	sun4v_vintr_get_target,#function
sun4v_vintr_get_target:
	mov	%o2, %g1
	mov	HV_FAST_VINTR_GET_TARGET, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%g1]
	retl
	 nop
	.size	sun4v_vintr_get_target, .-sun4v_vintr_get_target

	/* %o0: device handle
	 * %o1:	device INO
	 * %o2:	cpuid
	 *
	 * returns %o0: status
	 */
	.globl	sun4v_vintr_set_target
	.type	sun4v_vintr_set_target,#function
sun4v_vintr_set_target:
	mov	HV_FAST_VINTR_SET_TARGET, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
	.size	sun4v_vintr_set_target, .-sun4v_vintr_set_target

	/* %o0: NCS sub-function
	 * %o1:	sub-function arg real-address
	 * %o2:	sub-function arg size
	 *
	 * returns %o0:	status
	 */
	.globl	sun4v_ncs_request
	.type	sun4v_ncs_request,#function
sun4v_ncs_request:
	mov	HV_FAST_NCS_REQUEST, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
	.size	sun4v_ncs_request, .-sun4v_ncs_request

	.globl	sun4v_svc_send
	.type	sun4v_svc_send,#function
sun4v_svc_send:
	save	%sp, -192, %sp
	mov	%i0, %o0
	mov	%i1, %o1
	mov	%i2, %o2
	mov	HV_FAST_SVC_SEND, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%i3]
	ret
	restore
	.size	sun4v_svc_send, .-sun4v_svc_send

	.globl	sun4v_svc_recv
	.type	sun4v_svc_recv,#function
sun4v_svc_recv:
	save	%sp, -192, %sp
	mov	%i0, %o0
	mov	%i1, %o1
	mov	%i2, %o2
	mov	HV_FAST_SVC_RECV, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%i3]
	ret
	restore
	.size	sun4v_svc_recv, .-sun4v_svc_recv

	.globl	sun4v_svc_getstatus
	.type	sun4v_svc_getstatus,#function
sun4v_svc_getstatus:
	mov	HV_FAST_SVC_GETSTATUS, %o5
	mov	%o1, %o4
	ta	HV_FAST_TRAP
	stx	%o1, [%o4]
	retl
	 nop
	.size	sun4v_svc_getstatus, .-sun4v_svc_getstatus

	.globl	sun4v_svc_setstatus
	.type	sun4v_svc_setstatus,#function
sun4v_svc_setstatus:
	mov	HV_FAST_SVC_SETSTATUS, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
	.size	sun4v_svc_setstatus, .-sun4v_svc_setstatus

	.globl	sun4v_svc_clrstatus
	.type	sun4v_svc_clrstatus,#function
sun4v_svc_clrstatus:
	mov	HV_FAST_SVC_CLRSTATUS, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
	.size	sun4v_svc_clrstatus, .-sun4v_svc_clrstatus

	.globl	sun4v_mmustat_conf
	.type	sun4v_mmustat_conf,#function
sun4v_mmustat_conf:
	mov	%o1, %o4
	mov	HV_FAST_MMUSTAT_CONF, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%o4]
	retl
	 nop
	.size	sun4v_mmustat_conf, .-sun4v_mmustat_conf

	.globl	sun4v_mmustat_info
	.type	sun4v_mmustat_info,#function
sun4v_mmustat_info:
	mov	%o0, %o4
	mov	HV_FAST_MMUSTAT_INFO, %o5
	ta	HV_FAST_TRAP
	stx	%o1, [%o4]
	retl
	 nop
	.size	sun4v_mmustat_info, .-sun4v_mmustat_info

	.globl	sun4v_mmu_demap_all
	.type	sun4v_mmu_demap_all,#function
sun4v_mmu_demap_all:
	clr	%o0
	clr	%o1
	mov	HV_MMU_ALL, %o2
	mov	HV_FAST_MMU_DEMAP_ALL, %o5
	ta	HV_FAST_TRAP
	retl
	 nop
	.size	sun4v_mmu_demap_all, .-sun4v_mmu_demap_all
