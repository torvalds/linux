all: libtls.a

clean:
	rm -f *~ *.o *.d libtls.a

install:
	@echo Nothing to be made.


include ../lib.rules

CFLAGS += -DCONFIG_INTERNAL_LIBTOMMATH
CFLAGS += -DCONFIG_CRYPTO_INTERNAL

LIB_OBJS= \
	asn1.o \
	bignum.o \
	pkcs1.o \
	pkcs5.o \
	pkcs8.o \
	rsa.o \
	tlsv1_client.o \
	tlsv1_client_read.o \
	tlsv1_client_write.o \
	tlsv1_common.o \
	tlsv1_cred.o \
	tlsv1_record.o \
	tlsv1_server.o \
	tlsv1_server_read.o \
	tlsv1_server_write.o \
	x509v3.o


libtls.a: $(LIB_OBJS)
	$(AR) crT $@ $?

-include $(OBJS:%.o=%.d)
