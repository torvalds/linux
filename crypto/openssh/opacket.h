/* $OpenBSD: opacket.h,v 1.13 2018/07/06 09:03:02 sf Exp $ */
#ifndef _OPACKET_H
/* Written by Markus Friedl. Placed in the public domain.  */

/* Map old to new API */
void     ssh_packet_start(struct ssh *, u_char);
void     ssh_packet_put_char(struct ssh *, int ch);
void     ssh_packet_put_int(struct ssh *, u_int value);
void     ssh_packet_put_int64(struct ssh *, u_int64_t value);
void     ssh_packet_put_bignum2(struct ssh *, BIGNUM * value);
void     ssh_packet_put_ecpoint(struct ssh *, const EC_GROUP *, const EC_POINT *);
void     ssh_packet_put_string(struct ssh *, const void *buf, u_int len);
void     ssh_packet_put_cstring(struct ssh *, const char *str);
void     ssh_packet_put_raw(struct ssh *, const void *buf, u_int len);
void     ssh_packet_send(struct ssh *);

u_int	 ssh_packet_get_char(struct ssh *);
u_int	 ssh_packet_get_int(struct ssh *);
u_int64_t ssh_packet_get_int64(struct ssh *);
void     ssh_packet_get_bignum2(struct ssh *, BIGNUM * value);
void	 ssh_packet_get_ecpoint(struct ssh *, const EC_GROUP *, EC_POINT *);
void	*ssh_packet_get_string(struct ssh *, u_int *length_ptr);
char	*ssh_packet_get_cstring(struct ssh *, u_int *length_ptr);

/* don't allow remaining bytes after the end of the message */
#define ssh_packet_check_eom(ssh) \
do { \
	int _len = ssh_packet_remaining(ssh); \
	if (_len > 0) { \
		logit("Packet integrity error (%d bytes remaining) at %s:%d", \
		    _len ,__FILE__, __LINE__); \
		ssh_packet_disconnect(ssh, \
		    "Packet integrity error."); \
	} \
} while (0)

/* old API */
void	 packet_close(void);
u_int	 packet_get_char(void);
u_int	 packet_get_int(void);
void     packet_set_connection(int, int);
int	 packet_read_seqnr(u_int32_t *);
int	 packet_read_poll_seqnr(u_int32_t *);
void	 packet_process_incoming(const char *buf, u_int len);
void	 packet_write_wait(void);
void	 packet_write_poll(void);
void	 packet_read_expect(int expected_type);
#define packet_set_timeout(timeout, count) \
	ssh_packet_set_timeout(active_state, (timeout), (count))
#define packet_connection_is_on_socket() \
	ssh_packet_connection_is_on_socket(active_state)
#define packet_set_nonblocking() \
	ssh_packet_set_nonblocking(active_state)
#define packet_get_connection_in() \
	ssh_packet_get_connection_in(active_state)
#define packet_get_connection_out() \
	ssh_packet_get_connection_out(active_state)
#define packet_set_protocol_flags(protocol_flags) \
	ssh_packet_set_protocol_flags(active_state, (protocol_flags))
#define packet_get_protocol_flags() \
	ssh_packet_get_protocol_flags(active_state)
#define packet_start(type) \
	ssh_packet_start(active_state, (type))
#define packet_put_char(value) \
	ssh_packet_put_char(active_state, (value))
#define packet_put_int(value) \
	ssh_packet_put_int(active_state, (value))
#define packet_put_int64(value) \
	ssh_packet_put_int64(active_state, (value))
#define packet_put_string( buf, len) \
	ssh_packet_put_string(active_state, (buf), (len))
#define packet_put_cstring(str) \
	ssh_packet_put_cstring(active_state, (str))
#define packet_put_raw(buf, len) \
	ssh_packet_put_raw(active_state, (buf), (len))
#define packet_put_bignum2(value) \
	ssh_packet_put_bignum2(active_state, (value))
#define packet_send() \
	ssh_packet_send(active_state)
#define packet_read() \
	ssh_packet_read(active_state)
#define packet_get_int64() \
	ssh_packet_get_int64(active_state)
#define packet_get_bignum2(value) \
	ssh_packet_get_bignum2(active_state, (value))
#define packet_remaining() \
	ssh_packet_remaining(active_state)
#define packet_get_string(length_ptr) \
	ssh_packet_get_string(active_state, (length_ptr))
#define packet_get_string_ptr(length_ptr) \
	ssh_packet_get_string_ptr(active_state, (length_ptr))
#define packet_get_cstring(length_ptr) \
	ssh_packet_get_cstring(active_state, (length_ptr))
void	packet_send_debug(const char *, ...)
	    __attribute__((format(printf, 1, 2)));
void	packet_disconnect(const char *, ...)
	    __attribute__((format(printf, 1, 2)))
	    __attribute__((noreturn));
#define packet_have_data_to_write() \
	ssh_packet_have_data_to_write(active_state)
#define packet_not_very_much_data_to_write() \
	ssh_packet_not_very_much_data_to_write(active_state)
#define packet_set_interactive(interactive, qos_interactive, qos_bulk) \
	ssh_packet_set_interactive(active_state, (interactive), (qos_interactive), (qos_bulk))
#define packet_is_interactive() \
	ssh_packet_is_interactive(active_state)
#define packet_set_maxsize(s) \
	ssh_packet_set_maxsize(active_state, (s))
#define packet_inc_alive_timeouts() \
	ssh_packet_inc_alive_timeouts(active_state)
#define packet_set_alive_timeouts(ka) \
	ssh_packet_set_alive_timeouts(active_state, (ka))
#define packet_get_maxsize() \
	ssh_packet_get_maxsize(active_state)
#define packet_add_padding(pad) \
	sshpkt_add_padding(active_state, (pad))
#define packet_send_ignore(nbytes) \
	ssh_packet_send_ignore(active_state, (nbytes))
#define packet_set_server() \
	ssh_packet_set_server(active_state)
#define packet_set_authenticated() \
	ssh_packet_set_authenticated(active_state)
#define packet_get_input() \
	ssh_packet_get_input(active_state)
#define packet_get_output() \
	ssh_packet_get_output(active_state)
#define packet_check_eom() \
	ssh_packet_check_eom(active_state)
#define set_newkeys(mode) \
	ssh_set_newkeys(active_state, (mode))
#define packet_get_state(m) \
	ssh_packet_get_state(active_state, m)
#define packet_set_state(m) \
	ssh_packet_set_state(active_state, m)
#define packet_get_raw(lenp) \
        sshpkt_ptr(active_state, lenp)
#define packet_get_ecpoint(c,p) \
	ssh_packet_get_ecpoint(active_state, c, p)
#define packet_put_ecpoint(c,p) \
	ssh_packet_put_ecpoint(active_state, c, p)
#define packet_get_rekey_timeout() \
	ssh_packet_get_rekey_timeout(active_state)
#define packet_set_rekey_limits(x,y) \
	ssh_packet_set_rekey_limits(active_state, x, y)
#define packet_get_bytes(x,y) \
	ssh_packet_get_bytes(active_state, x, y)
#define packet_set_mux() \
	ssh_packet_set_mux(active_state)
#define packet_get_mux() \
	ssh_packet_get_mux(active_state)
#define packet_clear_keys() \
	ssh_packet_clear_keys(active_state)

#endif /* _OPACKET_H */
