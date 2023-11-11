/* SPDX-License-Identifier: MIT */
#ifndef __NVIF_UNPACK_H__
#define __NVIF_UNPACK_H__

#define nvif_unvers(r,d,s,m) ({                                                \
	void **_data = (d); __u32 *_size = (s); int _ret = (r);                \
	if (_ret == -ENOSYS && *_size == sizeof(m)) {                          \
		*_data = NULL;                                                 \
		*_size = _ret = 0;                                             \
	}                                                                      \
	_ret;                                                                  \
})

#define nvif_unpack(r,d,s,m,vl,vh,x) ({                                        \
	void **_data = (d); __u32 *_size = (s);                                \
	int _ret = (r), _vl = (vl), _vh = (vh);                                \
	if (_ret == -ENOSYS && *_size >= sizeof(m) &&                          \
	    (m).version >= _vl && (m).version <= _vh) {                        \
		*_data = (__u8 *)*_data + sizeof(m);                           \
		*_size = *_size - sizeof(m);                                   \
		if (_ret = 0, !(x)) {                                          \
			_ret = *_size ? -E2BIG : 0;                            \
			*_data = NULL;                                         \
			*_size = 0;                                            \
		}                                                              \
	}                                                                      \
	_ret;                                                                  \
})
#endif
