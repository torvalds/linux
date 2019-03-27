/* C99 union initialization */
union {
	int i[10];
	short s;
} c[] = { 
	{ s: 2 },
	{ i: { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 } },
};
