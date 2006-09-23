#ifndef _TYPES_H_
#define _TYPES_H_

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

typedef unsigned char		u8;
typedef unsigned short		u16;
typedef unsigned int		u32;
typedef unsigned long long	u64;

#define min(x,y) ({ \
	typeof(x) _x = (x);	\
	typeof(y) _y = (y);	\
	(void) (&_x == &_y);	\
	_x < _y ? _x : _y; })

#define max(x,y) ({ \
	typeof(x) _x = (x);	\
	typeof(y) _y = (y);	\
	(void) (&_x == &_y);	\
	_x > _y ? _x : _y; })

#endif /* _TYPES_H_ */
