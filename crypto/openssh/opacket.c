/* $OpenBSD: opacket.c,v 1.7 2017/10/20 01:56:39 djm Exp $ */
/* Written by Markus Friedl. Placed in the public domain.  */

#include "includes.h"

#include <stdarg.h>

#include "ssherr.h"
#include "packet.h"
#include "log.h"

struct ssh *active_state, *backup_state;

/* Map old to new API */

void
ssh_packet_start(struct ssh *ssh, u_char type)
{
	int r;

	if ((r = sshpkt_start(ssh, type)) != 0)
		fatal("%s: %s", __func__, ssh_err(r));
}

void
ssh_packet_put_char(struct ssh *ssh, int value)
{
	u_char ch = value;
	int r;

	if ((r = sshpkt_put_u8(ssh, ch)) != 0)
		fatal("%s: %s", __func__, ssh_err(r));
}

void
ssh_packet_put_int(struct ssh *ssh, u_int value)
{
	int r;

	if ((r = sshpkt_put_u32(ssh, value)) != 0)
		fatal("%s: %s", __func__, ssh_err(r));
}

void
ssh_packet_put_int64(struct ssh *ssh, u_int64_t value)
{
	int r;

	if ((r = sshpkt_put_u64(ssh, value)) != 0)
		fatal("%s: %s", __func__, ssh_err(r));
}

void
ssh_packet_put_string(struct ssh *ssh, const void *buf, u_int len)
{
	int r;

	if ((r = sshpkt_put_string(ssh, buf, len)) != 0)
		fatal("%s: %s", __func__, ssh_err(r));
}

void
ssh_packet_put_cstring(struct ssh *ssh, const char *str)
{
	int r;

	if ((r = sshpkt_put_cstring(ssh, str)) != 0)
		fatal("%s: %s", __func__, ssh_err(r));
}

void
ssh_packet_put_raw(struct ssh *ssh, const void *buf, u_int len)
{
	int r;

	if ((r = sshpkt_put(ssh, buf, len)) != 0)
		fatal("%s: %s", __func__, ssh_err(r));
}


#ifdef WITH_OPENSSL
void
ssh_packet_put_bignum2(struct ssh *ssh, BIGNUM * value)
{
	int r;

	if ((r = sshpkt_put_bignum2(ssh, value)) != 0)
		fatal("%s: %s", __func__, ssh_err(r));
}

# ifdef OPENSSL_HAS_ECC
void
ssh_packet_put_ecpoint(struct ssh *ssh, const EC_GROUP *curve,
    const EC_POINT *point)
{
	int r;

	if ((r = sshpkt_put_ec(ssh, point, curve)) != 0)
		fatal("%s: %s", __func__, ssh_err(r));
}
# endif
#endif /* WITH_OPENSSL */

void
ssh_packet_send(struct ssh *ssh)
{
	int r;

	if ((r = sshpkt_send(ssh)) != 0)
		fatal("%s: %s", __func__, ssh_err(r));
}

u_int
ssh_packet_get_char(struct ssh *ssh)
{
	u_char ch;
	int r;

	if ((r = sshpkt_get_u8(ssh, &ch)) != 0)
		fatal("%s: %s", __func__, ssh_err(r));
	return ch;
}

u_int
ssh_packet_get_int(struct ssh *ssh)
{
	u_int val;
	int r;

	if ((r = sshpkt_get_u32(ssh, &val)) != 0)
		fatal("%s: %s", __func__, ssh_err(r));
	return val;
}

u_int64_t
ssh_packet_get_int64(struct ssh *ssh)
{
	u_int64_t val;
	int r;

	if ((r = sshpkt_get_u64(ssh, &val)) != 0)
		fatal("%s: %s", __func__, ssh_err(r));
	return val;
}


#ifdef WITH_OPENSSL
void
ssh_packet_get_bignum2(struct ssh *ssh, BIGNUM * value)
{
	int r;

	if ((r = sshpkt_get_bignum2(ssh, value)) != 0)
		fatal("%s: %s", __func__, ssh_err(r));
}

# ifdef OPENSSL_HAS_ECC
void
ssh_packet_get_ecpoint(struct ssh *ssh, const EC_GROUP *curve, EC_POINT *point)
{
	int r;

	if ((r = sshpkt_get_ec(ssh, point, curve)) != 0)
		fatal("%s: %s", __func__, ssh_err(r));
}
# endif
#endif /* WITH_OPENSSL */

void *
ssh_packet_get_string(struct ssh *ssh, u_int *length_ptr)
{
	int r;
	size_t len;
	u_char *val;

	if ((r = sshpkt_get_string(ssh, &val, &len)) != 0)
		fatal("%s: %s", __func__, ssh_err(r));
	if (length_ptr != NULL)
		*length_ptr = (u_int)len;
	return val;
}

const void *
ssh_packet_get_string_ptr(struct ssh *ssh, u_int *length_ptr)
{
	int r;
	size_t len;
	const u_char *val;

	if ((r = sshpkt_get_string_direct(ssh, &val, &len)) != 0)
		fatal("%s: %s", __func__, ssh_err(r));
	if (length_ptr != NULL)
		*length_ptr = (u_int)len;
	return val;
}

char *
ssh_packet_get_cstring(struct ssh *ssh, u_int *length_ptr)
{
	int r;
	size_t len;
	char *val;

	if ((r = sshpkt_get_cstring(ssh, &val, &len)) != 0)
		fatal("%s: %s", __func__, ssh_err(r));
	if (length_ptr != NULL)
		*length_ptr = (u_int)len;
	return val;
}

/* Old API, that had to be reimplemented */

void
packet_set_connection(int fd_in, int fd_out)
{
	active_state = ssh_packet_set_connection(active_state, fd_in, fd_out);
	if (active_state == NULL)
		fatal("%s: ssh_packet_set_connection failed", __func__);
}

u_int
packet_get_char(void)
{
	return (ssh_packet_get_char(active_state));
}

u_int
packet_get_int(void)
{
	return (ssh_packet_get_int(active_state));
}

int
packet_read_seqnr(u_int32_t *seqnr)
{
	u_char type;
	int r;

	if ((r = ssh_packet_read_seqnr(active_state, &type, seqnr)) != 0)
		sshpkt_fatal(active_state, __func__, r);
	return type;
}

int
packet_read_poll_seqnr(u_int32_t *seqnr)
{
	u_char type;
	int r;

	if ((r = ssh_packet_read_poll_seqnr(active_state, &type, seqnr)))
		sshpkt_fatal(active_state, __func__, r);
	return type;
}

void
packet_close(void)
{
	ssh_packet_close(active_state);
	active_state = NULL;
}

void
packet_process_incoming(const char *buf, u_int len)
{
	int r;

	if ((r = ssh_packet_process_incoming(active_state, buf, len)) != 0)
		sshpkt_fatal(active_state, __func__, r);
}

void
packet_write_wait(void)
{
	int r;

	if ((r = ssh_packet_write_wait(active_state)) != 0)
		sshpkt_fatal(active_state, __func__, r);
}

void
packet_write_poll(void)
{
	int r;

	if ((r = ssh_packet_write_poll(active_state)) != 0)
		sshpkt_fatal(active_state, __func__, r);
}

void
packet_read_expect(int expected_type)
{
	int r;

	if ((r = ssh_packet_read_expect(active_state, expected_type)) != 0)
		sshpkt_fatal(active_state, __func__, r);
}

void
packet_disconnect(const char *fmt, ...)
{
	char buf[1024];
	va_list args;

	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	ssh_packet_disconnect(active_state, "%s", buf);
}

void
packet_send_debug(const char *fmt, ...)
{
	char buf[1024];
	va_list args;

	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	ssh_packet_send_debug(active_state, "%s", buf);
}
