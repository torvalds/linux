/* cast initialization as the rhs of a - operand */
struct sockaddr_dl {
	char sdl_data[2];
};

int             npdl_datasize = sizeof(struct sockaddr_dl) -
((int) ((unsigned long)&((struct sockaddr_dl *) 0)->sdl_data[0]));
