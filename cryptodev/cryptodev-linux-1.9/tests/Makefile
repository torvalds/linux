CFLAGS += -I.. $(CRYPTODEV_CFLAGS) -Wall -Werror

comp_progs := cipher_comp hash_comp hmac_comp

hostprogs := cipher cipher-aead hmac speed async_cipher async_hmac \
	async_speed sha_speed hashcrypt_speed fullspeed cipher-gcm \
	cipher-aead-srtp $(comp_progs)

example-cipher-objs := cipher.o
example-cipher-aead-objs := cipher-aead.o
example-hmac-objs := hmac.o
example-speed-objs := speed.c
example-fullspeed-objs := fullspeed.c
example-sha-speed-objs := sha_speed.c
example-async-cipher-objs := async_cipher.o
example-async-hmac-objs := async_hmac.o
example-async-speed-objs := async_speed.o
example-hashcrypt-speed-objs := hashcrypt_speed.c

prefix ?= /usr/local
execprefix ?= $(prefix)
bindir = $(execprefix)/bin

all: $(hostprogs)

check: $(hostprogs)
	./cipher
	./hmac
	./async_cipher
	./async_hmac
	./cipher-aead-srtp
	./cipher-gcm
	./cipher-aead

install:
	install -d $(DESTDIR)/$(bindir)
	for prog in $(hostprogs); do \
		install -m 755 $$prog $(DESTDIR)/$(bindir); \
	done

clean:
	rm -f *.o *~ $(hostprogs)

${comp_progs}: LDLIBS += -lssl -lcrypto
${comp_progs}: %: %.o openssl_wrapper.o

.PHONY: all clean check install
