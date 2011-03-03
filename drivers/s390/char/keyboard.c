/*
 *  drivers/s390/char/keyboard.c
 *    ebcdic keycode functions for s390 console drivers
 *
 *  S390 version
 *    Copyright (C) 2003 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com),
 */

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/sysrq.h>

#include <linux/consolemap.h>
#include <linux/kbd_kern.h>
#include <linux/kbd_diacr.h>
#include <asm/uaccess.h>

#include "keyboard.h"

/*
 * Handler Tables.
 */
#define K_HANDLERS\
	k_self,		k_fn,		k_spec,		k_ignore,\
	k_dead,		k_ignore,	k_ignore,	k_ignore,\
	k_ignore,	k_ignore,	k_ignore,	k_ignore,\
	k_ignore,	k_ignore,	k_ignore,	k_ignore

typedef void (k_handler_fn)(struct kbd_data *, unsigned char);
static k_handler_fn K_HANDLERS;
static k_handler_fn *k_handler[16] = { K_HANDLERS };

/* maximum values each key_handler can handle */
static const int kbd_max_vals[] = {
	255, ARRAY_SIZE(func_table) - 1, NR_FN_HANDLER - 1, 0,
	NR_DEAD - 1, 0, 0, 0, 0, 0, 0, 0, 0, 0
};
static const int KBD_NR_TYPES = ARRAY_SIZE(kbd_max_vals);

static unsigned char ret_diacr[NR_DEAD] = {
	'`', '\'', '^', '~', '"', ','
};

/*
 * Alloc/free of kbd_data structures.
 */
struct kbd_data *
kbd_alloc(void) {
	struct kbd_data *kbd;
	int i, len;

	kbd = kzalloc(sizeof(struct kbd_data), GFP_KERNEL);
	if (!kbd)
		goto out;
	kbd->key_maps = kzalloc(sizeof(key_maps), GFP_KERNEL);
	if (!kbd->key_maps)
		goto out_kbd;
	for (i = 0; i < ARRAY_SIZE(key_maps); i++) {
		if (key_maps[i]) {
			kbd->key_maps[i] =
				kmalloc(sizeof(u_short)*NR_KEYS, GFP_KERNEL);
			if (!kbd->key_maps[i])
				goto out_maps;
			memcpy(kbd->key_maps[i], key_maps[i],
			       sizeof(u_short)*NR_KEYS);
		}
	}
	kbd->func_table = kzalloc(sizeof(func_table), GFP_KERNEL);
	if (!kbd->func_table)
		goto out_maps;
	for (i = 0; i < ARRAY_SIZE(func_table); i++) {
		if (func_table[i]) {
			len = strlen(func_table[i]) + 1;
			kbd->func_table[i] = kmalloc(len, GFP_KERNEL);
			if (!kbd->func_table[i])
				goto out_func;
			memcpy(kbd->func_table[i], func_table[i], len);
		}
	}
	kbd->fn_handler =
		kzalloc(sizeof(fn_handler_fn *) * NR_FN_HANDLER, GFP_KERNEL);
	if (!kbd->fn_handler)
		goto out_func;
	kbd->accent_table =
		kmalloc(sizeof(struct kbdiacruc)*MAX_DIACR, GFP_KERNEL);
	if (!kbd->accent_table)
		goto out_fn_handler;
	memcpy(kbd->accent_table, accent_table,
	       sizeof(struct kbdiacruc)*MAX_DIACR);
	kbd->accent_table_size = accent_table_size;
	return kbd;

out_fn_handler:
	kfree(kbd->fn_handler);
out_func:
	for (i = 0; i < ARRAY_SIZE(func_table); i++)
		kfree(kbd->func_table[i]);
	kfree(kbd->func_table);
out_maps:
	for (i = 0; i < ARRAY_SIZE(key_maps); i++)
		kfree(kbd->key_maps[i]);
	kfree(kbd->key_maps);
out_kbd:
	kfree(kbd);
out:
	return NULL;
}

void
kbd_free(struct kbd_data *kbd)
{
	int i;

	kfree(kbd->accent_table);
	kfree(kbd->fn_handler);
	for (i = 0; i < ARRAY_SIZE(func_table); i++)
		kfree(kbd->func_table[i]);
	kfree(kbd->func_table);
	for (i = 0; i < ARRAY_SIZE(key_maps); i++)
		kfree(kbd->key_maps[i]);
	kfree(kbd->key_maps);
	kfree(kbd);
}

/*
 * Generate ascii -> ebcdic translation table from kbd_data.
 */
void
kbd_ascebc(struct kbd_data *kbd, unsigned char *ascebc)
{
	unsigned short *keymap, keysym;
	int i, j, k;

	memset(ascebc, 0x40, 256);
	for (i = 0; i < ARRAY_SIZE(key_maps); i++) {
		keymap = kbd->key_maps[i];
		if (!keymap)
			continue;
		for (j = 0; j < NR_KEYS; j++) {
			k = ((i & 1) << 7) + j;
			keysym = keymap[j];
			if (KTYP(keysym) == (KT_LATIN | 0xf0) ||
			    KTYP(keysym) == (KT_LETTER | 0xf0))
				ascebc[KVAL(keysym)] = k;
			else if (KTYP(keysym) == (KT_DEAD | 0xf0))
				ascebc[ret_diacr[KVAL(keysym)]] = k;
		}
	}
}

#if 0
/*
 * Generate ebcdic -> ascii translation table from kbd_data.
 */
void
kbd_ebcasc(struct kbd_data *kbd, unsigned char *ebcasc)
{
	unsigned short *keymap, keysym;
	int i, j, k;

	memset(ebcasc, ' ', 256);
	for (i = 0; i < ARRAY_SIZE(key_maps); i++) {
		keymap = kbd->key_maps[i];
		if (!keymap)
			continue;
		for (j = 0; j < NR_KEYS; j++) {
			keysym = keymap[j];
			k = ((i & 1) << 7) + j;
			if (KTYP(keysym) == (KT_LATIN | 0xf0) ||
			    KTYP(keysym) == (KT_LETTER | 0xf0))
				ebcasc[k] = KVAL(keysym);
			else if (KTYP(keysym) == (KT_DEAD | 0xf0))
				ebcasc[k] = ret_diacr[KVAL(keysym)];
		}
	}
}
#endif

/*
 * We have a combining character DIACR here, followed by the character CH.
 * If the combination occurs in the table, return the corresponding value.
 * Otherwise, if CH is a space or equals DIACR, return DIACR.
 * Otherwise, conclude that DIACR was not combining after all,
 * queue it and return CH.
 */
static unsigned int
handle_diacr(struct kbd_data *kbd, unsigned int ch)
{
	int i, d;

	d = kbd->diacr;
	kbd->diacr = 0;

	for (i = 0; i < kbd->accent_table_size; i++) {
		if (kbd->accent_table[i].diacr == d &&
		    kbd->accent_table[i].base == ch)
			return kbd->accent_table[i].result;
	}

	if (ch == ' ' || ch == d)
		return d;

	kbd_put_queue(kbd->tty, d);
	return ch;
}

/*
 * Handle dead key.
 */
static void
k_dead(struct kbd_data *kbd, unsigned char value)
{
	value = ret_diacr[value];
	kbd->diacr = (kbd->diacr ? handle_diacr(kbd, value) : value);
}

/*
 * Normal character handler.
 */
static void
k_self(struct kbd_data *kbd, unsigned char value)
{
	if (kbd->diacr)
		value = handle_diacr(kbd, value);
	kbd_put_queue(kbd->tty, value);
}

/*
 * Special key handlers
 */
static void
k_ignore(struct kbd_data *kbd, unsigned char value)
{
}

/*
 * Function key handler.
 */
static void
k_fn(struct kbd_data *kbd, unsigned char value)
{
	if (kbd->func_table[value])
		kbd_puts_queue(kbd->tty, kbd->func_table[value]);
}

static void
k_spec(struct kbd_data *kbd, unsigned char value)
{
	if (value >= NR_FN_HANDLER)
		return;
	if (kbd->fn_handler[value])
		kbd->fn_handler[value](kbd);
}

/*
 * Put utf8 character to tty flip buffer.
 * UTF-8 is defined for words of up to 31 bits,
 * but we need only 16 bits here
 */
static void
to_utf8(struct tty_struct *tty, ushort c) 
{
	if (c < 0x80)
		/*  0******* */
		kbd_put_queue(tty, c);
	else if (c < 0x800) {
		/* 110***** 10****** */
		kbd_put_queue(tty, 0xc0 | (c >> 6));
		kbd_put_queue(tty, 0x80 | (c & 0x3f));
	} else {
		/* 1110**** 10****** 10****** */
		kbd_put_queue(tty, 0xe0 | (c >> 12));
		kbd_put_queue(tty, 0x80 | ((c >> 6) & 0x3f));
		kbd_put_queue(tty, 0x80 | (c & 0x3f));
	}
}

/*
 * Process keycode.
 */
void
kbd_keycode(struct kbd_data *kbd, unsigned int keycode)
{
	unsigned short keysym;
	unsigned char type, value;

	if (!kbd || !kbd->tty)
		return;

	if (keycode >= 384)
		keysym = kbd->key_maps[5][keycode - 384];
	else if (keycode >= 256)
		keysym = kbd->key_maps[4][keycode - 256];
	else if (keycode >= 128)
		keysym = kbd->key_maps[1][keycode - 128];
	else
		keysym = kbd->key_maps[0][keycode];

	type = KTYP(keysym);
	if (type >= 0xf0) {
		type -= 0xf0;
		if (type == KT_LETTER)
			type = KT_LATIN;
		value = KVAL(keysym);
#ifdef CONFIG_MAGIC_SYSRQ	       /* Handle the SysRq Hack */
		if (kbd->sysrq) {
			if (kbd->sysrq == K(KT_LATIN, '-')) {
				kbd->sysrq = 0;
				handle_sysrq(value, kbd->tty);
				return;
			}
			if (value == '-') {
				kbd->sysrq = K(KT_LATIN, '-');
				return;
			}
			/* Incomplete sysrq sequence. */
			(*k_handler[KTYP(kbd->sysrq)])(kbd, KVAL(kbd->sysrq));
			kbd->sysrq = 0;
		} else if ((type == KT_LATIN && value == '^') ||
			   (type == KT_DEAD && ret_diacr[value] == '^')) {
			kbd->sysrq = K(type, value);
			return;
		}
#endif
		(*k_handler[type])(kbd, value);
	} else
		to_utf8(kbd->tty, keysym);
}

/*
 * Ioctl stuff.
 */
static int
do_kdsk_ioctl(struct kbd_data *kbd, struct kbentry __user *user_kbe,
	      int cmd, int perm)
{
	struct kbentry tmp;
	ushort *key_map, val, ov;

	if (copy_from_user(&tmp, user_kbe, sizeof(struct kbentry)))
		return -EFAULT;
#if NR_KEYS < 256
	if (tmp.kb_index >= NR_KEYS)
		return -EINVAL;
#endif
#if MAX_NR_KEYMAPS < 256
	if (tmp.kb_table >= MAX_NR_KEYMAPS)
		return -EINVAL;	
#endif

	switch (cmd) {
	case KDGKBENT:
		key_map = kbd->key_maps[tmp.kb_table];
		if (key_map) {
		    val = U(key_map[tmp.kb_index]);
		    if (KTYP(val) >= KBD_NR_TYPES)
			val = K_HOLE;
		} else
		    val = (tmp.kb_index ? K_HOLE : K_NOSUCHMAP);
		return put_user(val, &user_kbe->kb_value);
	case KDSKBENT:
		if (!perm)
			return -EPERM;
		if (!tmp.kb_index && tmp.kb_value == K_NOSUCHMAP) {
			/* disallocate map */
			key_map = kbd->key_maps[tmp.kb_table];
			if (key_map) {
			    kbd->key_maps[tmp.kb_table] = NULL;
			    kfree(key_map);
			}
			break;
		}

		if (KTYP(tmp.kb_value) >= KBD_NR_TYPES)
			return -EINVAL;
		if (KVAL(tmp.kb_value) > kbd_max_vals[KTYP(tmp.kb_value)])
			return -EINVAL;

		if (!(key_map = kbd->key_maps[tmp.kb_table])) {
			int j;

			key_map = kmalloc(sizeof(plain_map),
						     GFP_KERNEL);
			if (!key_map)
				return -ENOMEM;
			kbd->key_maps[tmp.kb_table] = key_map;
			for (j = 0; j < NR_KEYS; j++)
				key_map[j] = U(K_HOLE);
		}
		ov = U(key_map[tmp.kb_index]);
		if (tmp.kb_value == ov)
			break;	/* nothing to do */
		/*
		 * Attention Key.
		 */
		if (((ov == K_SAK) || (tmp.kb_value == K_SAK)) &&
		    !capable(CAP_SYS_ADMIN))
			return -EPERM;
		key_map[tmp.kb_index] = U(tmp.kb_value);
		break;
	}
	return 0;
}

static int
do_kdgkb_ioctl(struct kbd_data *kbd, struct kbsentry __user *u_kbs,
	       int cmd, int perm)
{
	unsigned char kb_func;
	char *p;
	int len;

	/* Get u_kbs->kb_func. */
	if (get_user(kb_func, &u_kbs->kb_func))
		return -EFAULT;
#if MAX_NR_FUNC < 256
	if (kb_func >= MAX_NR_FUNC)
		return -EINVAL;
#endif

	switch (cmd) {
	case KDGKBSENT:
		p = kbd->func_table[kb_func];
		if (p) {
			len = strlen(p);
			if (len >= sizeof(u_kbs->kb_string))
				len = sizeof(u_kbs->kb_string) - 1;
			if (copy_to_user(u_kbs->kb_string, p, len))
				return -EFAULT;
		} else
			len = 0;
		if (put_user('\0', u_kbs->kb_string + len))
			return -EFAULT;
		break;
	case KDSKBSENT:
		if (!perm)
			return -EPERM;
		len = strnlen_user(u_kbs->kb_string,
				   sizeof(u_kbs->kb_string) - 1);
		if (!len)
			return -EFAULT;
		if (len > sizeof(u_kbs->kb_string) - 1)
			return -EINVAL;
		p = kmalloc(len + 1, GFP_KERNEL);
		if (!p)
			return -ENOMEM;
		if (copy_from_user(p, u_kbs->kb_string, len)) {
			kfree(p);
			return -EFAULT;
		}
		p[len] = 0;
		kfree(kbd->func_table[kb_func]);
		kbd->func_table[kb_func] = p;
		break;
	}
	return 0;
}

int
kbd_ioctl(struct kbd_data *kbd, struct file *file,
	  unsigned int cmd, unsigned long arg)
{
	void __user *argp;
	unsigned int ct;
	int perm;

	argp = (void __user *)arg;

	/*
	 * To have permissions to do most of the vt ioctls, we either have
	 * to be the owner of the tty, or have CAP_SYS_TTY_CONFIG.
	 */
	perm = current->signal->tty == kbd->tty || capable(CAP_SYS_TTY_CONFIG);
	switch (cmd) {
	case KDGKBTYPE:
		return put_user(KB_101, (char __user *)argp);
	case KDGKBENT:
	case KDSKBENT:
		return do_kdsk_ioctl(kbd, argp, cmd, perm);
	case KDGKBSENT:
	case KDSKBSENT:
		return do_kdgkb_ioctl(kbd, argp, cmd, perm);
	case KDGKBDIACR:
	{
		struct kbdiacrs __user *a = argp;
		struct kbdiacr diacr;
		int i;

		if (put_user(kbd->accent_table_size, &a->kb_cnt))
			return -EFAULT;
		for (i = 0; i < kbd->accent_table_size; i++) {
			diacr.diacr = kbd->accent_table[i].diacr;
			diacr.base = kbd->accent_table[i].base;
			diacr.result = kbd->accent_table[i].result;
			if (copy_to_user(a->kbdiacr + i, &diacr, sizeof(struct kbdiacr)))
			return -EFAULT;
		}
		return 0;
	}
	case KDGKBDIACRUC:
	{
		struct kbdiacrsuc __user *a = argp;

		ct = kbd->accent_table_size;
		if (put_user(ct, &a->kb_cnt))
			return -EFAULT;
		if (copy_to_user(a->kbdiacruc, kbd->accent_table,
				 ct * sizeof(struct kbdiacruc)))
			return -EFAULT;
		return 0;
	}
	case KDSKBDIACR:
	{
		struct kbdiacrs __user *a = argp;
		struct kbdiacr diacr;
		int i;

		if (!perm)
			return -EPERM;
		if (get_user(ct, &a->kb_cnt))
			return -EFAULT;
		if (ct >= MAX_DIACR)
			return -EINVAL;
		kbd->accent_table_size = ct;
		for (i = 0; i < ct; i++) {
			if (copy_from_user(&diacr, a->kbdiacr + i, sizeof(struct kbdiacr)))
				return -EFAULT;
			kbd->accent_table[i].diacr = diacr.diacr;
			kbd->accent_table[i].base = diacr.base;
			kbd->accent_table[i].result = diacr.result;
		}
		return 0;
	}
	case KDSKBDIACRUC:
	{
		struct kbdiacrsuc __user *a = argp;

		if (!perm)
			return -EPERM;
		if (get_user(ct, &a->kb_cnt))
			return -EFAULT;
		if (ct >= MAX_DIACR)
			return -EINVAL;
		kbd->accent_table_size = ct;
		if (copy_from_user(kbd->accent_table, a->kbdiacruc,
				   ct * sizeof(struct kbdiacruc)))
			return -EFAULT;
		return 0;
	}
	default:
		return -ENOIOCTLCMD;
	}
}

EXPORT_SYMBOL(kbd_ioctl);
EXPORT_SYMBOL(kbd_ascebc);
EXPORT_SYMBOL(kbd_free);
EXPORT_SYMBOL(kbd_alloc);
EXPORT_SYMBOL(kbd_keycode);
