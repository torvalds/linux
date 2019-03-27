/* C99 recursive struct/union initialization */
struct top {
	int i;
	char c;
	union onion {
		short us;
		char uc;
	}  u;
	char *s;
} c[] = { 
	{ .s = "foo", .c = 'b', .u = { .uc = 'c' } },
	{ .i = 1, .c = 'a', .u = { .us = 2 } },
};
