/* C9X array initializers */
int foo[256] = {
	[2] = 1,
	[3] = 2,
	[4 ... 5] = 3
};
