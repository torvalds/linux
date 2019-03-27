#undef YYBTYACC
#define YYBTYACC 0
#define YYDEBUGSTR YYPREFIX "debug"
typedef short YYINT;
const YYINT error_lhs[] = {                       -1,
    0,
};
const YYINT error_len[] = {                        2,
    1,
};
const YYINT error_defred[] = {                     0,
    1,    0,
};
#if defined(YYDESTRUCT_CALL) || defined(YYSTYPE_TOSTRING)
const YYINT error_stos[] = {                       0,
  256,  258,
};
#endif /* YYDESTRUCT_CALL || YYSTYPE_TOSTRING */
const YYINT error_dgoto[] = {                      2,
};
const YYINT error_sindex[] = {                  -256,
    0,    0,
};
const YYINT error_rindex[] = {                     0,
    0,    0,
};
#if YYBTYACC
const YYINT error_cindex[] = {                     0,
    0,    0,
};
#endif
const YYINT error_gindex[] = {                     0,
};
const YYINT error_table[] = {                      1,
};
const YYINT error_check[] = {                    256,
};
#if YYBTYACC
const YYINT error_ctable[] = {                    -1,
};
#endif
#ifndef YYDEBUG
#define YYDEBUG 0
#endif
#if YYDEBUG
const char *const error_name[] = {

"$end",0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,"error","$accept","S","illegal-symbol",
};
const char *const error_rule[] = {
"$accept : S",
"S : error",

};
#endif
