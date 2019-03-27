/* C99 union initialization */
union {
	int i;
	char *s;
} c[] = { 
	{ i: 1 },
	{ s: "foo" }
};
