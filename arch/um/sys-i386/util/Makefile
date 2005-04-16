
hostprogs-y	:= mk_sc mk_thread
always		:= $(hostprogs-y)

mk_thread-objs	:= mk_thread_kern.o mk_thread_user.o

HOSTCFLAGS_mk_thread_kern.o	:= $(CFLAGS) $(CPPFLAGS)
HOSTCFLAGS_mk_thread_user.o	:= $(USER_CFLAGS)
