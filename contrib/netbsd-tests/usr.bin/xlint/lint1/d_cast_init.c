/* cast initialization */
typedef unsigned char u_char;
typedef unsigned int size_t;
struct	sockaddr_x25 {
	u_char	x25_len;
	u_char	x25_family;	 
	short	x25_net;	 
	char	x25_addr[16];	 
	struct	x25opts {
		char	op_flags;	 
		char	op_psize;	 
		char	op_wsize;	 
		char	op_speed;	 
	} x25_opts;
	short	x25_udlen;	 
	char	x25_udata[16];	 
};

struct sockaddr_x25 x25_dgmask = {
	(unsigned char)(unsigned char)(unsigned int)(unsigned long)(&((( struct sockaddr_x25  *)0)->x25_udata[1])) ,	 
	0,		 
	0,		 
	{-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},	 
	{0, 0, 0, 0},		 
	-1,		 
	{-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},	 
};
