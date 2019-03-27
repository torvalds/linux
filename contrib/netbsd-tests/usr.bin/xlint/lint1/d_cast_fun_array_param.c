
static void f(void *b[4]) {
	(void)&b;
}

void *
foo(void *fn) {
	return fn == 0 ? f : (void (*)(void *[4])) fn;
}
