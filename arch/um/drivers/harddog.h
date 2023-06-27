/* SPDX-License-Identifier: GPL-2.0 */
#ifndef UM_WATCHDOG_H
#define UM_WATCHDOG_H

int start_watchdog(int *in_fd_ret, int *out_fd_ret, char *sock);
void stop_watchdog(int in_fd, int out_fd);
int ping_watchdog(int fd);

#endif /* UM_WATCHDOG_H */
