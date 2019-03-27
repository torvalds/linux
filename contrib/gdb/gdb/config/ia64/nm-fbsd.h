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

#include "target.h"

#define	NATIVE_XFER_DIRTY	ia64_fbsd_xfer_dirty
extern LONGEST ia64_fbsd_xfer_dirty(struct target_ops *, enum target_object,
    const char *, void *, const void *, ULONGEST, LONGEST);

#endif /* NM_FBSD_H */
