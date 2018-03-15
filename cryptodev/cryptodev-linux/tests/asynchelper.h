#ifndef __ASYNCHELPER_H
#define __ASYNCHELPER_H

/* poll until POLLOUT, then call CIOCASYNCCRYPT */
inline int do_async_crypt(int cfd, struct crypt_op *cryp)
{
	struct pollfd pfd;

	pfd.fd = cfd;
	pfd.events = POLLOUT;

	if (poll(&pfd, 1, -1) < 1) {
		perror("poll()");
		return 1;
	}

	if (ioctl(cfd, CIOCASYNCCRYPT, cryp)) {
		perror("ioctl(CIOCCRYPT)");
		return 1;
	}
	return 0;
}

/* poll until POLLIN, then call CIOCASYNCFETCH */
inline int do_async_fetch(int cfd, struct crypt_op *cryp)
{
	struct pollfd pfd;

	pfd.fd = cfd;
	pfd.events = POLLIN;

	if (poll(&pfd, 1, -1) < 1) {
		perror("poll()");
		return 1;
	}

	if (ioctl(cfd, CIOCASYNCFETCH, cryp)) {
		perror("ioctl(CIOCCRYPT)");
		return 1;
	}
	return 0;
}

/* Check return value of stmt for identity with goodval. If they
 * don't match, call return with the value of stmt. */
#define DO_OR_DIE(stmt, goodval) {                           \
	int __rc_val;                                        \
	if ((__rc_val = stmt) != goodval) {                  \
		perror("DO_OR_DIE(" #stmt "," #goodval ")"); \
		return __rc_val;                             \
	}                                                    \
}

#endif /* __ASYNCHELPER_H */
