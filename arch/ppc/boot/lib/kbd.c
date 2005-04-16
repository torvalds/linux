#include <linux/keyboard.h>

#include "defkeymap.c"	/* yeah I know it's bad -- Cort */


unsigned char shfts, ctls, alts, caps;

#define	KBDATAP		0x60	/* kbd data port */
#define	KBSTATUSPORT	0x61	/* kbd status */
#define	KBSTATP		0x64	/* kbd status port */
#define	KBINRDY		0x01
#define	KBOUTRDY	0x02

extern unsigned char inb(int port);
extern void outb(int port, char val);
extern void puts(const char *);
extern void puthex(unsigned long val);
extern void udelay(long x);

static int kbd(int noblock)
{
	unsigned char dt, brk, val;
	unsigned code;
loop:
	if (noblock) {
	    if ((inb(KBSTATP) & KBINRDY) == 0)
		return (-1);
	} else while((inb(KBSTATP) & KBINRDY) == 0) ;

	dt = inb(KBDATAP);

	brk = dt & 0x80;	/* brk == 1 on key release */
	dt = dt & 0x7f;		/* keycode */

	if (shfts)
	    code = shift_map[dt];
	else if (ctls)
	    code = ctrl_map[dt];
	else
	    code = plain_map[dt];

	val = KVAL(code);
	switch (KTYP(code) & 0x0f) {
	    case KT_LATIN:
		if (brk)
		    break;
		if (alts)
		    val |= 0x80;
		if (val == 0x7f)	/* map delete to backspace */
		    val = '\b';
		return val;

	    case KT_LETTER:
		if (brk)
		    break;
		if (caps)
		    val -= 'a'-'A';
		return val;

	    case KT_SPEC:
		if (brk)
		    break;
		if (val == KVAL(K_CAPS))
		    caps = !caps;
		else if (val == KVAL(K_ENTER)) {
enter:		    /* Wait for key up */
		    while (1) {
			while((inb(KBSTATP) & KBINRDY) == 0) ;
			dt = inb(KBDATAP);
			if (dt & 0x80) /* key up */ break;
		    }
		    return 10;
		}
		break;

	    case KT_PAD:
		if (brk)
		    break;
		if (val < 10)
		    return val;
		if (val == KVAL(K_PENTER))
		    goto enter;
		break;

	    case KT_SHIFT:
		switch (val) {
		    case KG_SHIFT:
		    case KG_SHIFTL:
		    case KG_SHIFTR:
			shfts = brk ? 0 : 1;
			break;
		    case KG_ALT:
		    case KG_ALTGR:
			alts = brk ? 0 : 1;
			break;
		    case KG_CTRL:
		    case KG_CTRLL:
		    case KG_CTRLR:
			ctls = brk ? 0 : 1;
			break;
		}
		break;

	    case KT_LOCK:
		switch (val) {
		    case KG_SHIFT:
		    case KG_SHIFTL:
		    case KG_SHIFTR:
			if (brk)
			    shfts = !shfts;
			break;
		    case KG_ALT:
		    case KG_ALTGR:
			if (brk)
			    alts = !alts;
			break;
		    case KG_CTRL:
		    case KG_CTRLL:
		    case KG_CTRLR:
			if (brk)
			    ctls = !ctls;
			break;
		}
		break;
	}
	if (brk) return (-1);  /* Ignore initial 'key up' codes */
	goto loop;
}

static int __kbdreset(void)
{
	unsigned char c;
	int i, t;

	/* flush input queue */
	t = 2000;
	while ((inb(KBSTATP) & KBINRDY))
	{
		(void)inb(KBDATAP);
		if (--t == 0)
			return 1;
	}
	/* Send self-test */
	t = 20000;
	while (inb(KBSTATP) & KBOUTRDY)
		if (--t == 0)
			return 2;
	outb(KBSTATP,0xAA);
	t = 200000;
	while ((inb(KBSTATP) & KBINRDY) == 0)	/* wait input ready */
		if (--t == 0)
			return 3;
	if ((c = inb(KBDATAP)) != 0x55)
	{
		puts("Keyboard self test failed - result:");
		puthex(c);
		puts("\n");
	}
	/* Enable interrupts and keyboard controller */
	t = 20000;
	while (inb(KBSTATP) & KBOUTRDY)
		if (--t == 0) return 4;
	outb(KBSTATP,0x60);
	t = 20000;
	while (inb(KBSTATP) & KBOUTRDY)
		if (--t == 0) return 5;
	outb(KBDATAP,0x45);
	for (i = 0;  i < 10000;  i++) udelay(1);

	t = 20000;
	while (inb(KBSTATP) & KBOUTRDY)
		if (--t == 0) return 6;
	outb(KBSTATP,0x20);
	t = 200000;
	while ((inb(KBSTATP) & KBINRDY) == 0)	/* wait input ready */
		if (--t == 0) return 7;
	if (! (inb(KBDATAP) & 0x40)) {
		/*
		 * Quote from PS/2 System Reference Manual:
		 *
		 * "Address hex 0060 and address hex 0064 should be
		 * written only when the input-buffer-full bit and
		 * output-buffer-full bit in the Controller Status
		 * register are set 0." (KBINRDY and KBOUTRDY)
		 */
		t = 200000;
		while (inb(KBSTATP) & (KBINRDY | KBOUTRDY))
			if (--t == 0) return 8;
		outb(KBDATAP,0xF0);
		t = 200000;
		while (inb(KBSTATP) & (KBINRDY | KBOUTRDY))
			if (--t == 0) return 9;
		outb(KBDATAP,0x01);
	}
	t = 20000;
	while (inb(KBSTATP) & KBOUTRDY)
		if (--t == 0) return 10;
	outb(KBSTATP,0xAE);
	return 0;
}

static void kbdreset(void)
{
	int ret = __kbdreset();

	if (ret) {
		puts("__kbdreset failed: ");
		puthex(ret);
		puts("\n");
	}
}

/* We have to actually read the keyboard when CRT_tstc is called,
 * since the pending data might be a key release code, and therefore
 * not valid data.  In this case, kbd() will return -1, even though there's
 * data to be read.  Of course, we might actually read a valid key press,
 * in which case it gets queued into key_pending for use by CRT_getc.
 */

static int kbd_reset = 0;

static int key_pending = -1;

int CRT_getc(void)
{
	int c;
	if (!kbd_reset) {kbdreset(); kbd_reset++; }

        if (key_pending != -1) {
                c = key_pending;
                key_pending = -1;
                return c;
        } else {
	while ((c = kbd(0)) == 0) ;
                return c;
        }
}

int CRT_tstc(void)
{
	if (!kbd_reset) {kbdreset(); kbd_reset++; }

        while (key_pending == -1 && ((inb(KBSTATP) & KBINRDY) != 0)) {
                key_pending = kbd(1);
        }

        return (key_pending != -1);
}
