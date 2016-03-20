/*
 * Copyright (c) 2013, Cisco Systems, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#ifndef USNIC_TRANSPORT_H_
#define USNIC_TRANSPORT_H_

#include "usnic_abi.h"

const char *usnic_transport_to_str(enum usnic_transport_type trans_type);
/*
 * Returns number of bytes written, excluding null terminator. If
 * nothing was written, the function returns 0.
 */
int usnic_transport_sock_to_str(char *buf, int buf_sz,
					struct socket *sock);
/*
 * Reserve a port. If "port_num" is set, then the function will try
 * to reserve that particular port.
 */
u16 usnic_transport_rsrv_port(enum usnic_transport_type type, u16 port_num);
void usnic_transport_unrsrv_port(enum usnic_transport_type type, u16 port_num);
/*
 * Do a fget on the socket refered to by sock_fd and returns the socket.
 * Socket will not be destroyed before usnic_transport_put_socket has
 * been called.
 */
struct socket *usnic_transport_get_socket(int sock_fd);
void usnic_transport_put_socket(struct socket *sock);
/*
 * Call usnic_transport_get_socket before calling *_sock_get_addr
 */
int usnic_transport_sock_get_addr(struct socket *sock, int *proto,
					uint32_t *addr, uint16_t *port);
int usnic_transport_init(void);
void usnic_transport_fini(void);
#endif /* !USNIC_TRANSPORT_H */
