hostprogs-y		:= mk_task mk_constants
always			:= $(hostprogs-y)

mk_task-objs		:= mk_task_user.o mk_task_kern.o
mk_constants-objs	:= mk_constants_user.o mk_constants_kern.o

HOSTCFLAGS_mk_task_kern.o	:= $(CFLAGS) $(CPPFLAGS)
HOSTCFLAGS_mk_constants_kern.o	:= $(CFLAGS) $(CPPFLAGS)
