/*
 * Copyright (c) 2016 Thomas Pornin <pornin@bolet.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining 
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be 
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, 
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND 
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>

#define SOCKET           int
#define INVALID_SOCKET   (-1)
#endif

#include "brssl.h"

static void
dump_blob(const char *name, const void *data, size_t len)
{
	const unsigned char *buf;
	size_t u;

	buf = data;
	fprintf(stderr, "%s (len = %lu)", name, (unsigned long)len);
	for (u = 0; u < len; u ++) {
		if ((u & 15) == 0) {
			fprintf(stderr, "\n%08lX  ", (unsigned long)u);
		} else if ((u & 7) == 0) {
			fprintf(stderr, " ");
		}
		fprintf(stderr, " %02x", buf[u]);
	}
	fprintf(stderr, "\n");
}

/*
 * Inspect the provided data in case it is a "command" to trigger a
 * special behaviour. If the command is recognised, then it is executed
 * and this function returns 1. Otherwise, this function returns 0.
 */
static int
run_command(br_ssl_engine_context *cc, unsigned char *buf, size_t len)
{
	/*
	 * A single static slot for saving session parameters.
	 */
	static br_ssl_session_parameters slot;
	static int slot_used = 0;

	size_t u;

	if (len < 2 || len > 3) {
		return 0;
	}
	if (len == 3 && (buf[1] != '\r' || buf[2] != '\n')) {
		return 0;
	}
	if (len == 2 && buf[1] != '\n') {
		return 0;
	}
	switch (buf[0]) {
	case 'Q':
		fprintf(stderr, "closing...\n");
		br_ssl_engine_close(cc);
		return 1;
	case 'R':
		if (br_ssl_engine_renegotiate(cc)) {
			fprintf(stderr, "renegotiating...\n");
		} else {
			fprintf(stderr, "not renegotiating.\n");
		}
		return 1;
	case 'F':
		/*
		 * Session forget is nominally client-only. But the
		 * session parameters are in the engine structure, which
		 * is the first field of the client context, so the cast
		 * still works properly. On the server, this forgetting
		 * has no effect.
		 */
		fprintf(stderr, "forgetting session...\n");
		br_ssl_client_forget_session((br_ssl_client_context *)cc);
		return 1;
	case 'S':
		fprintf(stderr, "saving session parameters...\n");
		br_ssl_engine_get_session_parameters(cc, &slot);
		fprintf(stderr, "  id = ");
		for (u = 0; u < slot.session_id_len; u ++) {
			fprintf(stderr, "%02X", slot.session_id[u]);
		}
		fprintf(stderr, "\n");
		slot_used = 1;
		return 1;
	case 'P':
		if (slot_used) {
			fprintf(stderr, "restoring session parameters...\n");
			fprintf(stderr, "  id = ");
			for (u = 0; u < slot.session_id_len; u ++) {
				fprintf(stderr, "%02X", slot.session_id[u]);
			}
			fprintf(stderr, "\n");
			br_ssl_engine_set_session_parameters(cc, &slot);
			return 1;
		}
		return 0;
	default:
		return 0;
	}
}

#ifdef _WIN32

typedef struct {
	unsigned char buf[1024];
	size_t ptr, len;
} in_buffer;

static int
in_return_bytes(in_buffer *bb, unsigned char *buf, size_t len)
{
	if (bb->ptr < bb->len) {
		size_t clen;

		if (buf == NULL) {
			return 1;
		}
		clen = bb->len - bb->ptr;
		if (clen > len) {
			clen = len;
		}
		memcpy(buf, bb->buf + bb->ptr, clen);
		bb->ptr += clen;
		if (bb->ptr == bb->len) {
			bb->ptr = bb->len = 0;
		}
		return (int)clen;
	}
	return 0;
}

/*
 * A buffered version of in_read(), using a buffer to return only
 * full lines when feasible.
 */
static int
in_read_buffered(HANDLE h_in, in_buffer *bb, unsigned char *buf, size_t len)
{
	int n;

	if (len == 0) {
		return 0;
	}
	n = in_return_bytes(bb, buf, len);
	if (n != 0) {
		return n;
	}
	for (;;) {
		INPUT_RECORD inrec;
		DWORD v;

		if (!PeekConsoleInput(h_in, &inrec, 1, &v)) {
			fprintf(stderr, "ERROR: PeekConsoleInput()"
				" failed with 0x%08lX\n",
				(unsigned long)GetLastError());
			return -1;
		}
		if (v == 0) {
			return 0;
		}
		if (!ReadConsoleInput(h_in, &inrec, 1, &v)) {
			fprintf(stderr, "ERROR: ReadConsoleInput()"
				" failed with 0x%08lX\n",
				(unsigned long)GetLastError());
			return -1;
		}
		if (v == 0) {
			return 0;
		}
		if (inrec.EventType == KEY_EVENT
			&& inrec.Event.KeyEvent.bKeyDown)
		{
			int c;

			c = inrec.Event.KeyEvent.uChar.AsciiChar;
			if (c == '\n' || c == '\r' || c == '\t'
				|| (c >= 32 && c != 127))
			{
				if (c == '\r') {
					c = '\n';
				}
				bb->buf[bb->ptr ++] = (unsigned char)c;
				printf("%c", c);
				fflush(stdout);
				bb->len = bb->ptr;
				if (bb->len == sizeof bb->buf || c == '\n') {
					bb->ptr = 0;
					return in_return_bytes(bb, buf, len);
				}
			}
		}
	}
}

static int
in_avail_buffered(HANDLE h_in, in_buffer *bb)
{
	return in_read_buffered(h_in, bb, NULL, 1);
}

#endif

/* see brssl.h */
int
run_ssl_engine(br_ssl_engine_context *cc, unsigned long fd, unsigned flags)
{
	int hsdetails;
	int retcode;
	int verbose;
	int trace;
#ifdef _WIN32
	WSAEVENT fd_event;
	int can_send, can_recv;
	HANDLE h_in, h_out;
	in_buffer bb;
#endif

	hsdetails = 0;
	retcode = 0;
	verbose = (flags & RUN_ENGINE_VERBOSE) != 0;
	trace = (flags & RUN_ENGINE_TRACE) != 0;

	/*
	 * Print algorithm details.
	 */
	if (verbose) {
		const char *rngname;

		fprintf(stderr, "Algorithms:\n");
		br_prng_seeder_system(&rngname);
		fprintf(stderr, "   RNG:           %s\n", rngname);
		if (cc->iaes_cbcenc != 0) {
			fprintf(stderr, "   AES/CBC (enc): %s\n",
				get_algo_name(cc->iaes_cbcenc, 0));
		}
		if (cc->iaes_cbcdec != 0) {
			fprintf(stderr, "   AES/CBC (dec): %s\n",
				get_algo_name(cc->iaes_cbcdec, 0));
		}
		if (cc->iaes_ctr != 0) {
			fprintf(stderr, "   AES/CTR:       %s\n",
				get_algo_name(cc->iaes_cbcdec, 0));
		}
		if (cc->iaes_ctrcbc != 0) {
			fprintf(stderr, "   AES/CCM:       %s\n",
				get_algo_name(cc->iaes_ctrcbc, 0));
		}
		if (cc->ides_cbcenc != 0) {
			fprintf(stderr, "   DES/CBC (enc): %s\n",
				get_algo_name(cc->ides_cbcenc, 0));
		}
		if (cc->ides_cbcdec != 0) {
			fprintf(stderr, "   DES/CBC (dec): %s\n",
				get_algo_name(cc->ides_cbcdec, 0));
		}
		if (cc->ighash != 0) {
			fprintf(stderr, "   GHASH (GCM):   %s\n",
				get_algo_name(cc->ighash, 0));
		}
		if (cc->ichacha != 0) {
			fprintf(stderr, "   ChaCha20:      %s\n",
				get_algo_name(cc->ichacha, 0));
		}
		if (cc->ipoly != 0) {
			fprintf(stderr, "   Poly1305:      %s\n",
				get_algo_name(cc->ipoly, 0));
		}
		if (cc->iec != 0) {
			fprintf(stderr, "   EC:            %s\n",
				get_algo_name(cc->iec, 0));
		}
		if (cc->iecdsa != 0) {
			fprintf(stderr, "   ECDSA:         %s\n",
				get_algo_name(cc->iecdsa, 0));
		}
		if (cc->irsavrfy != 0) {
			fprintf(stderr, "   RSA (vrfy):    %s\n",
				get_algo_name(cc->irsavrfy, 0));
		}
	}

#ifdef _WIN32
	fd_event = WSA_INVALID_EVENT;
	can_send = 0;
	can_recv = 0;
	bb.ptr = bb.len = 0;
#endif

	/*
	 * On Unix systems, we need to follow three descriptors:
	 * standard input (0), standard output (1), and the socket
	 * itself (for both read and write). This is done with a poll()
	 * call.
	 *
	 * On Windows systems, we use WSAEventSelect() to associate
	 * an event handle with the network activity, and we use
	 * WaitForMultipleObjectsEx() on that handle and the standard
	 * input handle, when appropriate. Standard output is assumed
	 * to be always writeable, and standard input to be the console;
	 * this does not work well (or at all) with redirections (to
	 * pipes or files) but it should be enough for a debug tool
	 * (TODO: make something that handles redirections as well).
	 */

#ifdef _WIN32
	fd_event = WSACreateEvent();
	if (fd_event == WSA_INVALID_EVENT) {
		fprintf(stderr, "ERROR: WSACreateEvent() failed with %d\n",
			WSAGetLastError());
		retcode = -2;
		goto engine_exit;
	}
	WSAEventSelect(fd, fd_event, FD_READ | FD_WRITE | FD_CLOSE);
	h_in = GetStdHandle(STD_INPUT_HANDLE);
	h_out = GetStdHandle(STD_OUTPUT_HANDLE);
	SetConsoleMode(h_in, ENABLE_ECHO_INPUT
		| ENABLE_LINE_INPUT
		| ENABLE_PROCESSED_INPUT
		| ENABLE_PROCESSED_OUTPUT
		| ENABLE_WRAP_AT_EOL_OUTPUT);
#else
	/*
	 * Make sure that stdin and stdout are non-blocking.
	 */
	fcntl(0, F_SETFL, O_NONBLOCK);
	fcntl(1, F_SETFL, O_NONBLOCK);
#endif

	/*
	 * Perform the loop.
	 */
	for (;;) {
		unsigned st;
		int sendrec, recvrec, sendapp, recvapp;
#ifdef _WIN32
		HANDLE pfd[2];
		DWORD wt;
#else
		struct pollfd pfd[3];
		int n;
#endif
		size_t u, k_fd, k_in, k_out;
		int sendrec_ok, recvrec_ok, sendapp_ok, recvapp_ok;

		/*
		 * Get current engine state.
		 */
		st = br_ssl_engine_current_state(cc);
		if (st == BR_SSL_CLOSED) {
			int err;

			err = br_ssl_engine_last_error(cc);
			if (err == BR_ERR_OK) {
				if (verbose) {
					fprintf(stderr,
						"SSL closed normally\n");
				}
				retcode = 0;
				goto engine_exit;
			} else {
				fprintf(stderr, "ERROR: SSL error %d", err);
				retcode = err;
				if (err >= BR_ERR_SEND_FATAL_ALERT) {
					err -= BR_ERR_SEND_FATAL_ALERT;
					fprintf(stderr,
						" (sent alert %d)\n", err);
				} else if (err >= BR_ERR_RECV_FATAL_ALERT) {
					err -= BR_ERR_RECV_FATAL_ALERT;
					fprintf(stderr,
						" (received alert %d)\n", err);
				} else {
					const char *ename;

					ename = find_error_name(err, NULL);
					if (ename == NULL) {
						ename = "unknown";
					}
					fprintf(stderr, " (%s)\n", ename);
				}
				goto engine_exit;
			}
		}

		/*
		 * Compute descriptors that must be polled, depending
		 * on engine state.
		 */
		sendrec = ((st & BR_SSL_SENDREC) != 0);
		recvrec = ((st & BR_SSL_RECVREC) != 0);
		sendapp = ((st & BR_SSL_SENDAPP) != 0);
		recvapp = ((st & BR_SSL_RECVAPP) != 0);
		if (verbose && sendapp && !hsdetails) {
			char csn[80];
			const char *pname;

			fprintf(stderr, "Handshake completed\n");
			fprintf(stderr, "   version:               ");
			switch (cc->session.version) {
			case BR_SSL30:
				fprintf(stderr, "SSL 3.0");
				break;
			case BR_TLS10:
				fprintf(stderr, "TLS 1.0");
				break;
			case BR_TLS11:
				fprintf(stderr, "TLS 1.1");
				break;
			case BR_TLS12:
				fprintf(stderr, "TLS 1.2");
				break;
			default:
				fprintf(stderr, "unknown (0x%04X)",
					(unsigned)cc->session.version);
				break;
			}
			fprintf(stderr, "\n");
			get_suite_name_ext(
				cc->session.cipher_suite, csn, sizeof csn);
			fprintf(stderr, "   cipher suite:          %s\n", csn);
			if (uses_ecdhe(cc->session.cipher_suite)) {
				get_curve_name_ext(
					br_ssl_engine_get_ecdhe_curve(cc),
					csn, sizeof csn);
				fprintf(stderr,
					"   ECDHE curve:           %s\n", csn);
			}
			fprintf(stderr, "   secure renegotiation:  %s\n",
				cc->reneg == 1 ? "no" : "yes");
			pname = br_ssl_engine_get_selected_protocol(cc);
			if (pname != NULL) {
				fprintf(stderr,
					"   protocol name (ALPN):  %s\n",
					pname);
			}
			hsdetails = 1;
		}

		k_fd = (size_t)-1;
		k_in = (size_t)-1;
		k_out = (size_t)-1;

		u = 0;
#ifdef _WIN32
		/*
		 * If we recorded that we can send or receive data, and we
		 * want to do exactly that, then we don't wait; we just do
		 * it.
		 */
		recvapp_ok = 0;
		sendrec_ok = 0;
		recvrec_ok = 0;
		sendapp_ok = 0;

		if (sendrec && can_send) {
			sendrec_ok = 1;
		} else if (recvrec && can_recv) {
			recvrec_ok = 1;
		} else if (recvapp) {
			recvapp_ok = 1;
		} else if (sendapp && in_avail_buffered(h_in, &bb)) {
			sendapp_ok = 1;
		} else {
			/*
			 * If we cannot do I/O right away, then we must
			 * wait for some event, and try again.
			 */
			pfd[u] = (HANDLE)fd_event;
			k_fd = u;
			u ++;
			if (sendapp) {
				pfd[u] = h_in;
				k_in = u;
				u ++;
			}
			wt = WaitForMultipleObjectsEx(u, pfd,
				FALSE, INFINITE, FALSE);
			if (wt == WAIT_FAILED) {
				fprintf(stderr, "ERROR:"
					" WaitForMultipleObjectsEx()"
					" failed with 0x%08lX",
					(unsigned long)GetLastError());
				retcode = -2;
				goto engine_exit;
			}
			if (wt == k_fd) {
				WSANETWORKEVENTS e;

				if (WSAEnumNetworkEvents(fd, fd_event, &e)) {
					fprintf(stderr, "ERROR:"
						" WSAEnumNetworkEvents()"
						" failed with %d\n",
						WSAGetLastError());
					retcode = -2;
					goto engine_exit;
				}
				if (e.lNetworkEvents & (FD_WRITE | FD_CLOSE)) {
					can_send = 1;
				}
				if (e.lNetworkEvents & (FD_READ | FD_CLOSE)) {
					can_recv = 1;
				}
			}
			continue;
		}
#else
		if (sendrec || recvrec) {
			pfd[u].fd = fd;
			pfd[u].revents = 0;
			pfd[u].events = 0;
			if (sendrec) {
				pfd[u].events |= POLLOUT;
			}
			if (recvrec) {
				pfd[u].events |= POLLIN;
			}
			k_fd = u;
			u ++;
		}
		if (sendapp) {
			pfd[u].fd = 0;
			pfd[u].revents = 0;
			pfd[u].events = POLLIN;
			k_in = u;
			u ++;
		}
		if (recvapp) {
			pfd[u].fd = 1;
			pfd[u].revents = 0;
			pfd[u].events = POLLOUT;
			k_out = u;
			u ++;
		}
		n = poll(pfd, u, -1);
		if (n < 0) {
			if (errno == EINTR) {
				continue;
			}
			perror("ERROR: poll()");
			retcode = -2;
			goto engine_exit;
		}
		if (n == 0) {
			continue;
		}

		/*
		 * We transform closures/errors into read+write accesses
		 * so as to force the read() or write() call that will
		 * detect the situation.
		 */
		while (u -- > 0) {
			if (pfd[u].revents & (POLLERR | POLLHUP)) {
				pfd[u].revents |= POLLIN | POLLOUT;
			}
		}

		recvapp_ok = recvapp && (pfd[k_out].revents & POLLOUT) != 0;
		sendrec_ok = sendrec && (pfd[k_fd].revents & POLLOUT) != 0;
		recvrec_ok = recvrec && (pfd[k_fd].revents & POLLIN) != 0;
		sendapp_ok = sendapp && (pfd[k_in].revents & POLLIN) != 0;
#endif

		/*
		 * We give preference to outgoing data, on stdout and on
		 * the socket.
		 */
		if (recvapp_ok) {
			unsigned char *buf;
			size_t len;
#ifdef _WIN32
			DWORD wlen;
#else
			ssize_t wlen;
#endif

			buf = br_ssl_engine_recvapp_buf(cc, &len);
#ifdef _WIN32
			if (!WriteFile(h_out, buf, len, &wlen, NULL)) {
				if (verbose) {
					fprintf(stderr, "stdout closed...\n");
				}
				retcode = -2;
				goto engine_exit;
			}
#else
			wlen = write(1, buf, len);
			if (wlen <= 0) {
				if (verbose) {
					fprintf(stderr, "stdout closed...\n");
				}
				retcode = -2;
				goto engine_exit;
			}
#endif
			br_ssl_engine_recvapp_ack(cc, wlen);
			continue;
		}
		if (sendrec_ok) {
			unsigned char *buf;
			size_t len;
			int wlen;

			buf = br_ssl_engine_sendrec_buf(cc, &len);
			wlen = send(fd, buf, len, 0);
			if (wlen <= 0) {
#ifdef _WIN32
				int err;

				err = WSAGetLastError();
				if (err == EWOULDBLOCK
					|| err == WSAEWOULDBLOCK)
				{
					can_send = 0;
					continue;
				}
#else
				if (errno == EINTR || errno == EWOULDBLOCK) {
					continue;
				}
#endif
				if (verbose) {
					fprintf(stderr, "socket closed...\n");
				}
				retcode = -1;
				goto engine_exit;
			}
			if (trace) {
				dump_blob("Outgoing bytes", buf, wlen);
			}
			br_ssl_engine_sendrec_ack(cc, wlen);
			continue;
		}
		if (recvrec_ok) {
			unsigned char *buf;
			size_t len;
			int rlen;

			buf = br_ssl_engine_recvrec_buf(cc, &len);
			rlen = recv(fd, buf, len, 0);
			if (rlen == 0) {
				if (verbose) {
					fprintf(stderr, "socket closed...\n");
				}
				retcode = -1;
				goto engine_exit;
			}
			if (rlen < 0) {
#ifdef _WIN32
				int err;

				err = WSAGetLastError();
				if (err == EWOULDBLOCK
					|| err == WSAEWOULDBLOCK)
				{
					can_recv = 0;
					continue;
				}
#else
				if (errno == EINTR || errno == EWOULDBLOCK) {
					continue;
				}
#endif
				if (verbose) {
					fprintf(stderr, "socket broke...\n");
				}
				retcode = -1;
				goto engine_exit;
			}
			if (trace) {
				dump_blob("Incoming bytes", buf, rlen);
			}
			br_ssl_engine_recvrec_ack(cc, rlen);
			continue;
		}
		if (sendapp_ok) {
			unsigned char *buf;
			size_t len;
#ifdef _WIN32
			int rlen;
#else
			ssize_t rlen;
#endif

			buf = br_ssl_engine_sendapp_buf(cc, &len);
#ifdef _WIN32
			rlen = in_read_buffered(h_in, &bb, buf, len);
#else
			rlen = read(0, buf, len);
#endif
			if (rlen <= 0) {
				if (verbose) {
					fprintf(stderr, "stdin closed...\n");
				}
				br_ssl_engine_close(cc);
			} else if (!run_command(cc, buf, rlen)) {
				br_ssl_engine_sendapp_ack(cc, rlen);
			}
			br_ssl_engine_flush(cc, 0);
			continue;
		}

		/* We should never reach that point. */
		fprintf(stderr, "ERROR: poll() misbehaves\n");
		retcode = -2;
		goto engine_exit;
	}

	/*
	 * Release allocated structures.
	 */
engine_exit:
#ifdef _WIN32
	if (fd_event != WSA_INVALID_EVENT) {
		WSACloseEvent(fd_event);
	}
#endif
	return retcode;
}
