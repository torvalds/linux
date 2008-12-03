	.globl		utrap_trap
	.type		utrap_trap,#function
utrap_trap:		/* %g3=handler,%g4=level */
	TRAP_LOAD_THREAD_REG(%g6, %g1)
	ldx		[%g6 + TI_UTRAPS], %g1
	brnz,pt		%g1, invoke_utrap
	 nop

	ba,pt		%xcc, etrap
	 rd		%pc, %g7
	mov		%l4, %o1
        call		bad_trap
	 add		%sp, PTREGS_OFF, %o0
	ba,pt		%xcc, rtrap
	 nop

invoke_utrap:
	sllx		%g3, 3, %g3
	ldx		[%g1 + %g3], %g1
	save		%sp, -128, %sp
	rdpr		%tstate, %l6
	rdpr		%cwp, %l7
	andn		%l6, TSTATE_CWP, %l6
	wrpr		%l6, %l7, %tstate
	rdpr		%tpc, %l6
	rdpr		%tnpc, %l7
	wrpr		%g1, 0, %tnpc
	done
	.size		utrap_trap,.-utrap_trap
