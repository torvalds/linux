#ifndef _LIBIBVERBS_ALLOCA_H_
#define	_LIBIBVERBS_ALLOCA_H_
#include <stdlib.h>
#include <string.h>
#include <stdlib.h>

#define	strdupa(_s)						\
({								\
	char *_d;						\
	int _len;						\
								\
	_len = strlen(_s) + 1;					\
	_d = alloca(_len);					\
	if (_d)							\
		memcpy(_d, _s, _len);				\
	_d;							\
})
#endif	/* _LIBIBVERBS_ALLOCA_H_ */
