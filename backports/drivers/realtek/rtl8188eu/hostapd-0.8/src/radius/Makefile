all: libradius.a

clean:
	rm -f *~ *.o *.d libradius.a

install:
	@echo Nothing to be made.


include ../lib.rules

CFLAGS += -DCONFIG_IPV6

LIB_OBJS= \
	radius.o \
	radius_client.o \
	radius_server.o

libradius.a: $(LIB_OBJS)
	$(AR) crT $@ $?

-include $(OBJS:%.o=%.d)
