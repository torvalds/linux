/* match.h - interface to shell ##/%% matching code */

#ifndef SHELL_MATCH_H
#define SHELL_MATCH_H 1

PUSH_AND_SET_FUNCTION_VISIBILITY_TO_HIDDEN

//TODO! Why ash.c still uses internal version?!

enum {
	SCAN_MOVE_FROM_LEFT = (1 << 0),
	SCAN_MOVE_FROM_RIGHT = (1 << 1),
	SCAN_MATCH_LEFT_HALF = (1 << 2),
	SCAN_MATCH_RIGHT_HALF = (1 << 3),
};

char* FAST_FUNC scan_and_match(char *string, const char *pattern, unsigned flags);

static inline unsigned pick_scan(char op1, char op2)
{
	unsigned scan_flags;
	if (op1 == '#') {
		scan_flags = SCAN_MATCH_LEFT_HALF +
			(op1 == op2 ? SCAN_MOVE_FROM_RIGHT : SCAN_MOVE_FROM_LEFT);
	} else { /* % */
		scan_flags = SCAN_MATCH_RIGHT_HALF +
			(op1 == op2 ? SCAN_MOVE_FROM_LEFT : SCAN_MOVE_FROM_RIGHT);
	}
	return scan_flags;
}

POP_SAVED_FUNCTION_VISIBILITY

#endif
