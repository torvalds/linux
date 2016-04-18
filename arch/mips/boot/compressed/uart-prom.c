
extern void prom_putchar(unsigned char ch);

void putc(char c)
{
	prom_putchar(c);
}
