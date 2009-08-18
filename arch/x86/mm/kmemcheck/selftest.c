#include <linux/kernel.h>

#include "opcode.h"
#include "selftest.h"

struct selftest_opcode {
	unsigned int expected_size;
	const uint8_t *insn;
	const char *desc;
};

static const struct selftest_opcode selftest_opcodes[] = {
	/* REP MOVS */
	{1, "\xf3\xa4", 		"rep movsb <mem8>, <mem8>"},
	{4, "\xf3\xa5",			"rep movsl <mem32>, <mem32>"},

	/* MOVZX / MOVZXD */
	{1, "\x66\x0f\xb6\x51\xf8",	"movzwq <mem8>, <reg16>"},
	{1, "\x0f\xb6\x51\xf8",		"movzwq <mem8>, <reg32>"},

	/* MOVSX / MOVSXD */
	{1, "\x66\x0f\xbe\x51\xf8",	"movswq <mem8>, <reg16>"},
	{1, "\x0f\xbe\x51\xf8",		"movswq <mem8>, <reg32>"},

#ifdef CONFIG_X86_64
	/* MOVZX / MOVZXD */
	{1, "\x49\x0f\xb6\x51\xf8",	"movzbq <mem8>, <reg64>"},
	{2, "\x49\x0f\xb7\x51\xf8",	"movzbq <mem16>, <reg64>"},

	/* MOVSX / MOVSXD */
	{1, "\x49\x0f\xbe\x51\xf8",	"movsbq <mem8>, <reg64>"},
	{2, "\x49\x0f\xbf\x51\xf8",	"movsbq <mem16>, <reg64>"},
	{4, "\x49\x63\x51\xf8",		"movslq <mem32>, <reg64>"},
#endif
};

static bool selftest_opcode_one(const struct selftest_opcode *op)
{
	unsigned size;

	kmemcheck_opcode_decode(op->insn, &size);

	if (size == op->expected_size)
		return true;

	printk(KERN_WARNING "kmemcheck: opcode %s: expected size %d, got %d\n",
		op->desc, op->expected_size, size);
	return false;
}

static bool selftest_opcodes_all(void)
{
	bool pass = true;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(selftest_opcodes); ++i)
		pass = pass && selftest_opcode_one(&selftest_opcodes[i]);

	return pass;
}

bool kmemcheck_selftest(void)
{
	bool pass = true;

	pass = pass && selftest_opcodes_all();

	return pass;
}
