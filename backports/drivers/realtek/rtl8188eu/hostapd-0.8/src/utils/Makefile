all: libutils.a

clean:
	rm -f *~ *.o *.d libutils.a

install:
	@echo Nothing to be made.


include ../lib.rules

#CFLAGS += -DWPA_TRACE
CFLAGS += -DCONFIG_IPV6

LIB_OBJS= \
	base64.o \
	common.o \
	ip_addr.o \
	radiotap.o \
	trace.o \
	uuid.o \
	wpa_debug.o \
	wpabuf.o

# Pick correct OS wrapper implementation
LIB_OBJS += os_unix.o

# Pick correct event loop implementation
LIB_OBJS += eloop.o

# Pick correct edit implementation
LIB_OBJS += edit.o

#LIB_OBJS += pcsc_funcs.o

libutils.a: $(LIB_OBJS)
	$(AR) crT $@ $?

-include $(OBJS:%.o=%.d)
