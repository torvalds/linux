/* GNU GPL */

#ifndef NM_FBSD_H
#define	NM_FBSD_H

/* Type of the third argument to the `ptrace' system call.  */
#define PTRACE_ARG3_TYPE caddr_t

/* Override copies of {fetch,store}_inferior_registers in `infptrace.c'.  */
#define FETCH_INFERIOR_REGISTERS

/* We can attach and detach.  */
#define ATTACH_DETACH

/* Override child_pid_to_exec_file in 'inftarg.c'.  */
#define	CHILD_PID_TO_EXEC_FILE

#endif /* NM_FBSD_H */
