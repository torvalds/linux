#ifndef OPENSSL_COMPAT_H
#define OPENSSL_COMPAT_H

#if OPENSSL_VERSION_NUMBER < 0x10100000L

static inline BIO_METHOD *BIO_meth_new(int type, const char *name)
{
	BIO_METHOD *biom = calloc(1, sizeof(BIO_METHOD));

	if (biom != NULL) {
		biom->type = type;
		biom->name = name;
	}
	return biom;
}

#define BIO_meth_set_write(b, f) (b)->bwrite = (f)
#define BIO_meth_set_read(b, f) (b)->bread = (f)
#define BIO_meth_set_puts(b, f) (b)->bputs = (f)
#define BIO_meth_set_ctrl(b, f) (b)->ctrl = (f)
#define BIO_meth_set_create(b, f) (b)->create = (f)
#define BIO_meth_set_destroy(b, f) (b)->destroy = (f)

#define BIO_set_init(b, val) (b)->init = (val)
#define BIO_set_data(b, val) (b)->ptr = (val)
#define BIO_set_shutdown(b, val) (b)->shutdown = (val)
#define BIO_get_init(b) (b)->init
#define BIO_get_data(b) (b)->ptr
#define BIO_get_shutdown(b) (b)->shutdown

#define TLS_method SSLv23_method

#endif /* OPENSSL_VERSION_NUMBER < 0x10100000L */

#endif /* OPENSSL_COMPAT_H */
