/* Allow packed c99 flexible arrays */
struct {
	int x;
	char y[0];
} __packed foo;

