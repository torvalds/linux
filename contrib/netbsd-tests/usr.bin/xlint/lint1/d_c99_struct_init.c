/* C99 struct initialization */
struct {
	int i;
	char *s;
} c[] = { 
	{ .i =  2, },
	{ .s =  "foo" },
	{ .i =  1, .s = "bar" },
	{ .s =  "foo", .i = -1 },
};
