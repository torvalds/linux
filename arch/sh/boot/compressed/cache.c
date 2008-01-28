int cache_control(unsigned int command)
{
	volatile unsigned int *p = (volatile unsigned int *) 0x80000000;
	int i;

	for (i = 0; i < (32 * 1024); i += 32) {
		(void)*p;
		p += (32 / sizeof (int));
	}

	return 0;
}
