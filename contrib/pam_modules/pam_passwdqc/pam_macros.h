/*
 * These macros are partially based on Linux-PAM's <security/_pam_macros.h>,
 * which were organized by Cristian Gafton and I believe are in the public
 * domain.
 */

#if !defined(_PAM_MACROS_H) && !defined(_pam_overwrite)
#define _PAM_MACROS_H

#include <string.h>
#include <stdlib.h>

#define _pam_overwrite(x) \
	memset((x), 0, strlen((x)))

#define _pam_drop_reply(/* struct pam_response * */ reply, /* int */ replies) \
do { \
	int i; \
\
	for (i = 0; i < (replies); i++) \
	if ((reply)[i].resp) { \
		_pam_overwrite((reply)[i].resp); \
		free((reply)[i].resp); \
	} \
	if ((reply)) free((reply)); \
} while (0)

#endif
