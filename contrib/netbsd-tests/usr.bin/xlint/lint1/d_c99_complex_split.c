int b(double a) {
	return a == 0;
}
void a(void) {
    double _Complex z = 0;
    if (b(__real__ z) && b(__imag__ z))
	return;
}
