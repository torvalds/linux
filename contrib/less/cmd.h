/*
 * Copyright (C) 1984-2017  Mark Nudelman
 *
 * You may distribute under the terms of either the GNU General Public
 * License or the Less License, as specified in the README file.
 *
 * For more information, see the README file.
 */


#define MAX_USERCMD            1000
#define MAX_CMDLEN             16

#define A_B_LINE               2
#define A_B_SCREEN             3
#define A_B_SCROLL             4
#define A_B_SEARCH             5
#define A_DIGIT                6
#define A_DISP_OPTION          7
#define A_DEBUG                8
#define A_EXAMINE              9
#define A_FIRSTCMD             10
#define A_FREPAINT             11
#define A_F_LINE               12
#define A_F_SCREEN             13
#define A_F_SCROLL             14
#define A_F_SEARCH             15
#define A_GOEND                16
#define A_GOLINE               17
#define A_GOMARK               18
#define A_HELP                 19
#define A_NEXT_FILE            20
#define A_PERCENT              21
#define A_PREFIX               22
#define A_PREV_FILE            23
#define A_QUIT                 24
#define A_REPAINT              25
#define A_SETMARK              26
#define A_SHELL                27
#define A_STAT                 28
#define A_FF_LINE              29
#define A_BF_LINE              30
#define A_VERSION              31
#define A_VISUAL               32
#define A_F_WINDOW             33
#define A_B_WINDOW             34
#define A_F_BRACKET            35
#define A_B_BRACKET            36
#define A_PIPE                 37
#define A_INDEX_FILE           38
#define A_UNDO_SEARCH          39
#define A_FF_SCREEN            40
#define A_LSHIFT               41
#define A_RSHIFT               42
#define A_AGAIN_SEARCH         43
#define A_T_AGAIN_SEARCH       44
#define A_REVERSE_SEARCH       45
#define A_T_REVERSE_SEARCH     46
#define A_OPT_TOGGLE           47
#define A_OPT_SET              48
#define A_OPT_UNSET            49
#define A_F_FOREVER            50
#define A_GOPOS                51
#define A_REMOVE_FILE          52
#define A_NEXT_TAG             53
#define A_PREV_TAG             54
#define A_FILTER               55
#define A_F_UNTIL_HILITE       56
#define A_GOEND_BUF            57
#define A_LLSHIFT              58
#define A_RRSHIFT              59
#define A_CLRMARK              62
#define A_SETMARKBOT           63

#define A_INVALID              100
#define A_NOACTION             101
#define A_UINVALID             102
#define A_END_LIST             103
#define A_SPECIAL_KEY          104

#define A_SKIP                 127

#define A_EXTRA                0200


/* Line editing characters */

#define EC_BACKSPACE           1
#define EC_LINEKILL            2
#define EC_RIGHT               3
#define EC_LEFT                4
#define EC_W_LEFT              5
#define EC_W_RIGHT             6
#define EC_INSERT              7
#define EC_DELETE              8
#define EC_HOME                9
#define EC_END                 10
#define EC_W_BACKSPACE         11
#define EC_W_DELETE            12
#define EC_UP                  13
#define EC_DOWN                14
#define EC_EXPAND              15
#define EC_F_COMPLETE          17
#define EC_B_COMPLETE          18
#define EC_LITERAL             19
#define EC_ABORT               20

#define EC_NOACTION            101
#define EC_UINVALID            102

/* Flags for editchar() */
#define EC_PEEK                01
#define EC_NOHISTORY           02
#define EC_NOCOMPLETE          04
#define EC_NORIGHTLEFT         010

/* Environment variable stuff */
#define EV_OK                  01

/* Special keys (keys which output different strings on different terminals) */
#define SK_SPECIAL_KEY         CONTROL('K')
#define SK_RIGHT_ARROW         1
#define SK_LEFT_ARROW          2
#define SK_UP_ARROW            3
#define SK_DOWN_ARROW          4
#define SK_PAGE_UP             5
#define SK_PAGE_DOWN           6
#define SK_HOME                7
#define SK_END                 8
#define SK_DELETE              9
#define SK_INSERT              10
#define SK_CTL_LEFT_ARROW      11
#define SK_CTL_RIGHT_ARROW     12
#define SK_CTL_DELETE          13
#define SK_F1                  14
#define SK_BACKTAB             15
#define SK_CTL_BACKSPACE       16
#define SK_CONTROL_K           40
