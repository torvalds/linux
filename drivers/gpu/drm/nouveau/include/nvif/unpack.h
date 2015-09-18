#ifndef __NVIF_UNPACK_H__
#define __NVIF_UNPACK_H__

#define nvif_unvers(d) ({                                                      \
	ret = (size == sizeof(d)) ? 0 : -ENOSYS;                               \
	(ret == 0);                                                            \
})

#define nvif_unpack(d,vl,vh,m) ({                                              \
	if ((vl) == 0 || ret == -ENOSYS) {                                     \
		int _size = sizeof(d);                                         \
		if (_size <= size && (d).version >= (vl) &&                    \
				     (d).version <= (vh)) {                    \
			data = (u8 *)data + _size;                             \
			size = size - _size;                                   \
			ret = ((m) || !size) ? 0 : -E2BIG;                     \
		} else {                                                       \
			ret = -ENOSYS;                                         \
		}                                                              \
	}                                                                      \
	(ret == 0);                                                            \
})

#endif
