/**
 * \file configfile.c
 *
 *  configuration/rc/ini file handling.
 *
 * @addtogroup autoopts
 * @{
 */
/*
 *  This file is part of AutoOpts, a companion to AutoGen.
 *  AutoOpts is free software.
 *  AutoOpts is Copyright (C) 1992-2015 by Bruce Korb - all rights reserved
 *
 *  AutoOpts is available under any one of two licenses.  The license
 *  in use must be one of these two and the choice is under the control
 *  of the user of the license.
 *
 *   The GNU Lesser General Public License, version 3 or later
 *      See the files "COPYING.lgplv3" and "COPYING.gplv3"
 *
 *   The Modified Berkeley Software Distribution License
 *      See the file "COPYING.mbsd"
 *
 *  These files have the following sha256 sums:
 *
 *  8584710e9b04216a394078dc156b781d0b47e1729104d666658aecef8ee32e95  COPYING.gplv3
 *  4379e7444a0e2ce2b12dd6f5a52a27a4d02d39d247901d3285c88cf0d37f477b  COPYING.lgplv3
 *  13aa749a5b0a454917a944ed8fffc530b784f5ead522b1aacaf4ec8aa55a6239  COPYING.mbsd
 */

/* = = = START-STATIC-FORWARD = = = */
static void
file_preset(tOptions * opts, char const * fname, int dir);

static char *
handle_comment(char * txt);

static char *
handle_cfg(tOptions * opts, tOptState * ost, char * txt, int dir);

static char *
handle_directive(tOptions * opts, char * txt);

static char *
aoflags_directive(tOptions * opts, char * txt);

static char *
program_directive(tOptions * opts, char * txt);

static char *
handle_section(tOptions * opts, char * txt);

static int
parse_xml_encoding(char ** ppz);

static char *
trim_xml_text(char * intxt, char const * pznm, tOptionLoadMode mode);

static void
cook_xml_text(char * pzData);

static char *
handle_struct(tOptions * opts, tOptState * ost, char * txt, int dir);

static char const *
parse_keyword(tOptions * opts, char const * txt, tOptionValue * typ);

static char const *
parse_set_mem(tOptions * opts, char const * txt, tOptionValue * typ);

static char const *
parse_value(char const * txt, tOptionValue * typ);
/* = = = END-STATIC-FORWARD = = = */

/**
 *  Skip over some unknown attribute
 *  @param[in] txt   start of skpped text
 *  @returns   character after skipped text
 */
inline static char const *
skip_unkn(char const * txt)
{
    txt = BRK_END_XML_TOKEN_CHARS(txt);
    return (*txt == NUL) ? NULL : txt;
}

/*=export_func  configFileLoad
 *
 * what:  parse a configuration file
 * arg:   + char const * + fname + the file to load +
 *
 * ret_type:  const tOptionValue *
 * ret_desc:  An allocated, compound value structure
 *
 * doc:
 *  This routine will load a named configuration file and parse the
 *  text as a hierarchically valued option.  The option descriptor
 *  created from an option definition file is not used via this interface.
 *  The returned value is "named" with the input file name and is of
 *  type "@code{OPARG_TYPE_HIERARCHY}".  It may be used in calls to
 *  @code{optionGetValue()}, @code{optionNextValue()} and
 *  @code{optionUnloadNested()}.
 *
 * err:
 *  If the file cannot be loaded or processed, @code{NULL} is returned and
 *  @var{errno} is set.  It may be set by a call to either @code{open(2)}
 *  @code{mmap(2)} or other file system calls, or it may be:
 *  @itemize @bullet
 *  @item
 *  @code{ENOENT} - the file was not found.
 *  @item
 *  @code{ENOMSG} - the file was empty.
 *  @item
 *  @code{EINVAL} - the file contents are invalid -- not properly formed.
 *  @item
 *  @code{ENOMEM} - not enough memory to allocate the needed structures.
 *  @end itemize
=*/
const tOptionValue *
configFileLoad(char const * fname)
{
    tmap_info_t    cfgfile;
    tOptionValue * res = NULL;
    tOptionLoadMode save_mode = option_load_mode;

    char * txt = text_mmap(fname, PROT_READ, MAP_PRIVATE, &cfgfile);

    if (TEXT_MMAP_FAILED_ADDR(txt))
        return NULL; /* errno is set */

    option_load_mode = OPTION_LOAD_COOKED;
    res = optionLoadNested(txt, fname, strlen(fname));

    if (res == NULL) {
        int err = errno;
        text_munmap(&cfgfile);
        errno = err;
    } else
        text_munmap(&cfgfile);

    option_load_mode = save_mode;
    return res;
}


/*=export_func  optionFindValue
 *
 * what:  find a hierarcicaly valued option instance
 * arg:   + const tOptDesc * + odesc + an option with a nested arg type +
 * arg:   + char const *     + name  + name of value to find +
 * arg:   + char const *     + val   + the matching value    +
 *
 * ret_type:  const tOptionValue *
 * ret_desc:  a compound value structure
 *
 * doc:
 *  This routine will find an entry in a nested value option or configurable.
 *  It will search through the list and return a matching entry.
 *
 * err:
 *  The returned result is NULL and errno is set:
 *  @itemize @bullet
 *  @item
 *  @code{EINVAL} - the @code{pOptValue} does not point to a valid
 *  hierarchical option value.
 *  @item
 *  @code{ENOENT} - no entry matched the given name.
 *  @end itemize
=*/
const tOptionValue *
optionFindValue(const tOptDesc * odesc, char const * name, char const * val)
{
    const tOptionValue * res = NULL;

    if (  (odesc == NULL)
       || (OPTST_GET_ARGTYPE(odesc->fOptState) != OPARG_TYPE_HIERARCHY))  {
        errno = EINVAL;
    }

    else if (odesc->optCookie == NULL) {
        errno = ENOENT;
    }

    else do {
        tArgList *    argl  = odesc->optCookie;
        int           argct = argl->useCt;
        const void ** poptv = VOIDP(argl->apzArgs);

        if (argct == 0) {
            errno = ENOENT;
            break;
        }

        if (name == NULL) {
            res = (const tOptionValue *)*poptv;
            break;
        }

        while (--argct >= 0) {
            const tOptionValue * ov = *(poptv++);
            const tOptionValue * rv = optionGetValue(ov, name);

            if (rv == NULL)
                continue;

            if (val == NULL) {
                res = ov;
                break;
            }
        }
        if (res == NULL)
            errno = ENOENT;
    } while (false);

    return res;
}


/*=export_func  optionFindNextValue
 *
 * FIXME: the handling of 'pzName' and 'pzVal' is just wrong.
 *
 * what:  find a hierarcicaly valued option instance
 * arg:   + const tOptDesc * + odesc + an option with a nested arg type +
 * arg:   + const tOptionValue * + pPrevVal + the last entry +
 * arg:   + char const *     + name     + name of value to find +
 * arg:   + char const *     + value    + the matching value    +
 *
 * ret_type:  const tOptionValue *
 * ret_desc:  a compound value structure
 *
 * doc:
 *  This routine will find the next entry in a nested value option or
 *  configurable.  It will search through the list and return the next entry
 *  that matches the criteria.
 *
 * err:
 *  The returned result is NULL and errno is set:
 *  @itemize @bullet
 *  @item
 *  @code{EINVAL} - the @code{pOptValue} does not point to a valid
 *  hierarchical option value.
 *  @item
 *  @code{ENOENT} - no entry matched the given name.
 *  @end itemize
=*/
tOptionValue const *
optionFindNextValue(const tOptDesc * odesc, const tOptionValue * pPrevVal,
                    char const * pzName, char const * pzVal)
{
    bool old_found = false;
    const tOptionValue * res = NULL;

    (void)pzName;
    (void)pzVal;

    if (  (odesc == NULL)
       || (OPTST_GET_ARGTYPE(odesc->fOptState) != OPARG_TYPE_HIERARCHY))  {
        errno = EINVAL;
    }

    else if (odesc->optCookie == NULL) {
        errno = ENOENT;
    }

    else do {
        tArgList *    argl  = odesc->optCookie;
        int           ct    = argl->useCt;
        const void ** poptv = VOIDP(argl->apzArgs);

        while (--ct >= 0) {
            const tOptionValue * pOV = *(poptv++);
            if (old_found) {
                res = pOV;
                break;
            }
            if (pOV == pPrevVal)
                old_found = true;
        }
        if (res == NULL)
            errno = ENOENT;
    } while (false);

    return res;
}


/*=export_func  optionGetValue
 *
 * what:  get a specific value from a hierarcical list
 * arg:   + const tOptionValue * + pOptValue + a hierarchcal value +
 * arg:   + char const *         + valueName + name of value to get +
 *
 * ret_type:  const tOptionValue *
 * ret_desc:  a compound value structure
 *
 * doc:
 *  This routine will find an entry in a nested value option or configurable.
 *  If "valueName" is NULL, then the first entry is returned.  Otherwise,
 *  the first entry with a name that exactly matches the argument will be
 *  returned.  If there is no matching value, NULL is returned and errno is
 *  set to ENOENT. If the provided option value is not a hierarchical value,
 *  NULL is also returned and errno is set to EINVAL.
 *
 * err:
 *  The returned result is NULL and errno is set:
 *  @itemize @bullet
 *  @item
 *  @code{EINVAL} - the @code{pOptValue} does not point to a valid
 *  hierarchical option value.
 *  @item
 *  @code{ENOENT} - no entry matched the given name.
 *  @end itemize
=*/
tOptionValue const *
optionGetValue(tOptionValue const * oov, char const * vname)
{
    tArgList *           arg_list;
    const tOptionValue * res = NULL;

    if ((oov == NULL) || (oov->valType != OPARG_TYPE_HIERARCHY)) {
        errno = EINVAL;
        return res;
    }
    arg_list = oov->v.nestVal;

    if (arg_list->useCt > 0) {
        int           ct     = arg_list->useCt;
        const void ** ovlist = VOIDP(arg_list->apzArgs);

        if (vname == NULL) {
            res = (const tOptionValue *)*ovlist;

        } else do {
            const tOptionValue * opt_val = *(ovlist++);
            if (strcmp(opt_val->pzName, vname) == 0) {
                res = opt_val;
                break;
            }
        } while (--ct > 0);
    }
    if (res == NULL)
        errno = ENOENT;
    return res;
}

/*=export_func  optionNextValue
 *
 * what:  get the next value from a hierarchical list
 * arg:   + const tOptionValue * + pOptValue + a hierarchcal list value +
 * arg:   + const tOptionValue * + pOldValue + a value from this list   +
 *
 * ret_type:  const tOptionValue *
 * ret_desc:  a compound value structure
 *
 * doc:
 *  This routine will return the next entry after the entry passed in.  At the
 *  end of the list, NULL will be returned.  If the entry is not found on the
 *  list, NULL will be returned and "@var{errno}" will be set to EINVAL.
 *  The "@var{pOldValue}" must have been gotten from a prior call to this
 *  routine or to "@code{opitonGetValue()}".
 *
 * err:
 *  The returned result is NULL and errno is set:
 *  @itemize @bullet
 *  @item
 *  @code{EINVAL} - the @code{pOptValue} does not point to a valid
 *  hierarchical option value or @code{pOldValue} does not point to a
 *  member of that option value.
 *  @item
 *  @code{ENOENT} - the supplied @code{pOldValue} pointed to the last entry.
 *  @end itemize
=*/
tOptionValue const *
optionNextValue(tOptionValue const * ov_list,tOptionValue const * oov )
{
    tArgList *           arg_list;
    const tOptionValue * res = NULL;
    int                  err = EINVAL;

    if ((ov_list == NULL) || (ov_list->valType != OPARG_TYPE_HIERARCHY)) {
        errno = EINVAL;
        return NULL;
    }
    arg_list = ov_list->v.nestVal;
    {
        int           ct     = arg_list->useCt;
        const void ** o_list = VOIDP(arg_list->apzArgs);

        while (ct-- > 0) {
            const tOptionValue * nov = *(o_list++);
            if (nov == oov) {
                if (ct == 0) {
                    err = ENOENT;

                } else {
                    err = 0;
                    res = (const tOptionValue *)*o_list;
                }
                break;
            }
        }
    }
    if (err != 0)
        errno = err;
    return res;
}

/**
 *  Load a file containing presetting information (a configuration file).
 */
static void
file_preset(tOptions * opts, char const * fname, int dir)
{
    tmap_info_t       cfgfile;
    tOptState         optst = OPTSTATE_INITIALIZER(PRESET);
    opt_state_mask_t  st_flags = optst.flags;
    opt_state_mask_t  fl_save  = opts->fOptSet;
    char *            ftext =
        text_mmap(fname, PROT_READ|PROT_WRITE, MAP_PRIVATE, &cfgfile);

    if (TEXT_MMAP_FAILED_ADDR(ftext))
        return;

    /*
     * While processing config files, we ignore errors.
     */
    opts->fOptSet &= ~OPTPROC_ERRSTOP;

    if (dir == DIRECTION_CALLED) {
        st_flags = OPTST_DEFINED;
        dir   = DIRECTION_PROCESS;
    }

    /*
     *  IF this is called via "optionProcess", then we are presetting.
     *  This is the default and the PRESETTING bit will be set.
     *  If this is called via "optionFileLoad", then the bit is not set
     *  and we consider stuff set herein to be "set" by the client program.
     */
    if ((opts->fOptSet & OPTPROC_PRESETTING) == 0)
        st_flags = OPTST_SET;

    do  {
        optst.flags = st_flags;
        ftext = SPN_WHITESPACE_CHARS(ftext);

        if (IS_VAR_FIRST_CHAR(*ftext)) {
            ftext = handle_cfg(opts, &optst, ftext, dir);

        } else switch (*ftext) {
        case '<':
            if (IS_VAR_FIRST_CHAR(ftext[1]))
                ftext = handle_struct(opts, &optst, ftext, dir);

            else switch (ftext[1]) {
            case '?':
                ftext = handle_directive(opts, ftext);
                break;

            case '!':
                ftext = handle_comment(ftext);
                break;

            case '/':
                ftext = strchr(ftext + 2, '>');
                if (ftext++ != NULL)
                    break;

            default:
                ftext = NULL;
            }
            if (ftext == NULL)
                goto all_done;
            break;

        case '[':
            ftext = handle_section(opts, ftext);
            break;

        case '#':
            ftext = strchr(ftext + 1, NL);
            break;

        default:
            goto all_done; /* invalid format */
        }
    } while (ftext != NULL);

 all_done:
    text_munmap(&cfgfile);
    opts->fOptSet = fl_save;
}

/**
 *  "txt" points to a "<!" sequence.
 *  Theoretically, we should ensure that it begins with "<!--",
 *  but actually I don't care that much.  It ends with "-->".
 */
static char *
handle_comment(char * txt)
{
    char * pz = strstr(txt, "-->");
    if (pz != NULL)
        pz += 3;
    return pz;
}

/**
 *  "txt" points to the start of some value name.
 *  The end of the entry is the end of the line that is not preceded by
 *  a backslash escape character.  The string value is always processed
 *  in "cooked" mode.
 */
static char *
handle_cfg(tOptions * opts, tOptState * ost, char * txt, int dir)
{
    char * pzName = txt++;
    char * pzEnd  = strchr(txt, NL);

    if (pzEnd == NULL)
        return txt + strlen(txt);

    txt = SPN_VALUE_NAME_CHARS(txt);
    txt = SPN_WHITESPACE_CHARS(txt);
    if (txt > pzEnd) {
    name_only:
        *pzEnd++ = NUL;
        load_opt_line(opts, ost, pzName, dir, OPTION_LOAD_UNCOOKED);
        return pzEnd;
    }

    /*
     *  Either the first character after the name is a ':' or '=',
     *  or else we must have skipped over white space.  Anything else
     *  is an invalid format and we give up parsing the text.
     */
    if ((*txt == '=') || (*txt == ':')) {
        txt = SPN_WHITESPACE_CHARS(txt+1);
        if (txt > pzEnd)
            goto name_only;
    } else if (! IS_WHITESPACE_CHAR(txt[-1]))
        return NULL;

    /*
     *  IF the value is continued, remove the backslash escape and push "pzEnd"
     *  on to a newline *not* preceded by a backslash.
     */
    if (pzEnd[-1] == '\\') {
        char * pcD = pzEnd-1;
        char * pcS = pzEnd;

        for (;;) {
            char ch = *(pcS++);
            switch (ch) {
            case NUL:
                pcS = NULL;
                /* FALLTHROUGH */

            case NL:
                *pcD = NUL;
                pzEnd = pcS;
                goto copy_done;

            case '\\':
                if (*pcS == NL)
                    ch = *(pcS++);
                /* FALLTHROUGH */
            default:
                *(pcD++) = ch;
            }
        } copy_done:;

    } else {
        /*
         *  The newline was not preceded by a backslash.  NUL it out
         */
        *(pzEnd++) = NUL;
    }

    /*
     *  "pzName" points to what looks like text for one option/configurable.
     *  It is NUL terminated.  Process it.
     */
    load_opt_line(opts, ost, pzName, dir, OPTION_LOAD_UNCOOKED);

    return pzEnd;
}

/**
 *  "txt" points to a "<?" sequence.
 *  We handle "<?program" and "<?auto-options" directives.
 *  All others are treated as comments.
 *
 *  @param[in,out] opts  program option descriptor
 *  @param[in]     txt   scanning pointer
 *  @returns       the next character to look at
 */
static char *
handle_directive(tOptions * opts, char * txt)
{
#   define DIRECTIVE_TABLE                      \
    _dt_(zCfgProg,     program_directive)       \
    _dt_(zCfgAO_Flags, aoflags_directive)

    typedef char * (directive_func_t)(tOptions *, char *);
#   define _dt_(_s, _fn) _fn,
    static directive_func_t * dir_disp[] = {
        DIRECTIVE_TABLE
    };
#   undef  _dt_

#   define _dt_(_s, _fn) 1 +
    static int  const   dir_ct  = DIRECTIVE_TABLE 0;
    static char const * dir_names[DIRECTIVE_TABLE 0];
#   undef _dt_

    int    ix;

    if (dir_names[0] == NULL) {
        ix = 0;
#   define _dt_(_s, _fn) dir_names[ix++] = _s;
        DIRECTIVE_TABLE;
#   undef _dt_
    }

    for (ix = 0; ix < dir_ct; ix++) {
        size_t len = strlen(dir_names[ix]);
        if (  (strncmp(txt + 2, dir_names[ix], len) == 0)
           && (! IS_VALUE_NAME_CHAR(txt[len+2])) )
            return dir_disp[ix](opts, txt + len + 2);
    }

    /*
     *  We don't know what this is.  Skip it.
     */
    txt = strchr(txt+2, '>');
    if (txt != NULL)
        txt++;
    return txt;
#   undef DIRECTIVE_TABLE
}

/**
 *  handle AutoOpts mode flags.
 *
 *  @param[in,out] opts  program option descriptor
 *  @param[in]     txt   scanning pointer
 *  @returns       the next character to look at
 */
static char *
aoflags_directive(tOptions * opts, char * txt)
{
    char * pz;

    pz = SPN_WHITESPACE_CHARS(txt+1);
    txt = strchr(pz, '>');
    if (txt != NULL) {

        size_t len  = (unsigned)(txt - pz);
        char * ftxt = AGALOC(len + 1, "aoflags");

        memcpy(ftxt, pz, len);
        ftxt[len] = NUL;
        set_usage_flags(opts, ftxt);
        AGFREE(ftxt);

        txt++;
    }

    return txt;
}

/**
 * handle program segmentation of config file.
 *
 *  @param[in,out] opts  program option descriptor
 *  @param[in]     txt   scanning pointer
 *  @returns       the next character to look at
 */
static char *
program_directive(tOptions * opts, char * txt)
{
    static char const ttlfmt[] = "<?";
    size_t ttl_len  = sizeof(ttlfmt) + strlen(zCfgProg);
    char * ttl      = AGALOC(ttl_len, "prog title");
    size_t name_len = strlen(opts->pzProgName);

    memcpy(ttl, ttlfmt, sizeof(ttlfmt) - 1);
    memcpy(ttl + sizeof(ttlfmt) - 1, zCfgProg, ttl_len - (sizeof(ttlfmt) - 1));

    do  {
        txt = SPN_WHITESPACE_CHARS(txt+1);

        if (  (strneqvcmp(txt, opts->pzProgName, (int)name_len) == 0)
           && (IS_END_XML_TOKEN_CHAR(txt[name_len])) ) {
            txt += name_len;
            break;
        }

        txt = strstr(txt, ttl);
    } while (txt != NULL);

    AGFREE(ttl);
    if (txt != NULL)
        for (;;) {
            if (*txt == NUL) {
                txt = NULL;
                break;
            }
            if (*(txt++) == '>')
                break;
        }

    return txt;
}

/**
 *  "txt" points to a '[' character.
 *  The "traditional" [PROG_NAME] segmentation of the config file.
 *  Do not ever mix with the "<?program prog-name>" variation.
 *
 *  @param[in,out] opts  program option descriptor
 *  @param[in]     txt   scanning pointer
 *  @returns       the next character to look at
 */
static char *
handle_section(tOptions * opts, char * txt)
{
    size_t len = strlen(opts->pzPROGNAME);
    if (   (strncmp(txt+1, opts->pzPROGNAME, len) == 0)
        && (txt[len+1] == ']'))
        return strchr(txt + len + 2, NL);

    if (len > 16)
        return NULL;

    {
        char z[24];
        sprintf(z, "[%s]", opts->pzPROGNAME);
        txt = strstr(txt, z);
    }

    if (txt != NULL)
        txt = strchr(txt, NL);
    return txt;
}

/**
 * parse XML encodings
 */
static int
parse_xml_encoding(char ** ppz)
{
#   define XMLTABLE             \
        _xmlNm_(amp,   '&')     \
        _xmlNm_(lt,    '<')     \
        _xmlNm_(gt,    '>')     \
        _xmlNm_(ff,    '\f')    \
        _xmlNm_(ht,    '\t')    \
        _xmlNm_(cr,    '\r')    \
        _xmlNm_(vt,    '\v')    \
        _xmlNm_(bel,   '\a')    \
        _xmlNm_(nl,    NL)      \
        _xmlNm_(space, ' ')     \
        _xmlNm_(quot,  '"')     \
        _xmlNm_(apos,  '\'')

    static struct {
        char const * const  nm_str;
        unsigned short      nm_len;
        short               nm_val;
    } const xml_names[] = {
#   define _xmlNm_(_n, _v) { #_n ";", sizeof(#_n), _v },
        XMLTABLE
#   undef  _xmlNm_
#   undef XMLTABLE
    };

    static int const nm_ct = sizeof(xml_names) / sizeof(xml_names[0]);
    int    base = 10;

    char * pz = *ppz;

    if (*pz == '#') {
        pz++;
        goto parse_number;
    }

    if (IS_DEC_DIGIT_CHAR(*pz)) {
        unsigned long v;

    parse_number:
        switch (*pz) {
        case 'x': case 'X':
            /*
             * Some forms specify hex with:  &#xNN;
             */
            base = 16;
            pz++;
            break;

        case '0':
            /*
             *  &#0022; is hex and &#22; is decimal.  Cool.
             *  Ya gotta love it.
             */
            if (pz[1] == '0')
                base = 16;
            break;
        }

        v = strtoul(pz, &pz, base);
        if ((*pz != ';') || (v > 0x7F))
            return NUL;
        *ppz = pz + 1;
        return (int)v;
    }

    {
        int ix = 0;
        do  {
            if (strncmp(pz, xml_names[ix].nm_str, xml_names[ix].nm_len)
                == 0) {
                *ppz = pz + xml_names[ix].nm_len;
                return xml_names[ix].nm_val;
            }
        } while (++ix < nm_ct);
    }

    return NUL;
}

/**
 * Find the end marker for the named section of XML.
 * Trim that text there, trimming trailing white space for all modes
 * except for OPTION_LOAD_UNCOOKED.
 */
static char *
trim_xml_text(char * intxt, char const * pznm, tOptionLoadMode mode)
{
    static char const fmt[] = "</%s>";
    size_t len = strlen(pznm) + sizeof(fmt) - 2 /* for %s */;
    char * etext;

    {
        char z[64], *pz = z;
        if (len >= sizeof(z))
            pz = AGALOC(len, "scan name");

        len = (size_t)sprintf(pz, fmt, pznm);
        *intxt = ' ';
        etext = strstr(intxt, pz);
        if (pz != z) AGFREE(pz);
    }

    if (etext == NULL)
        return etext;

    {
        char * result = etext + len;

        if (mode != OPTION_LOAD_UNCOOKED)
            etext = SPN_WHITESPACE_BACK(intxt, etext);

        *etext = NUL;
        return result;
    }
}

/**
 */
static void
cook_xml_text(char * pzData)
{
    char * pzs = pzData;
    char * pzd = pzData;
    char   bf[4];
    bf[2] = NUL;

    for (;;) {
        int ch = ((int)*(pzs++)) & 0xFF;
        switch (ch) {
        case NUL:
            *pzd = NUL;
            return;

        case '&':
            ch = parse_xml_encoding(&pzs);
            *(pzd++) = (char)ch;
            if (ch == NUL)
                return;
            break;

        case '%':
            bf[0] = *(pzs++);
            bf[1] = *(pzs++);
            if ((bf[0] == NUL) || (bf[1] == NUL)) {
                *pzd = NUL;
                return;
            }

            ch = (int)strtoul(bf, NULL, 16);
            /* FALLTHROUGH */

        default:
            *(pzd++) = (char)ch;
        }
    }
}

/**
 *  "txt" points to a '<' character, followed by an alpha.
 *  The end of the entry is either the "/>" following the name, or else a
 *  "</name>" string.
 */
static char *
handle_struct(tOptions * opts, tOptState * ost, char * txt, int dir)
{
    tOptionLoadMode mode = option_load_mode;
    tOptionValue    valu;

    char * pzName = ++txt;
    char * pzData;
    char * pcNulPoint;

    txt = SPN_VALUE_NAME_CHARS(txt);
    pcNulPoint = txt;
    valu.valType = OPARG_TYPE_STRING;

    switch (*txt) {
    case ' ':
    case '\t':
        txt = VOIDP(parse_attrs(
            opts, SPN_WHITESPACE_CHARS(txt), &mode, &valu));
        if (txt == NULL)
            return txt;
        if (*txt == '>')
            break;
        if (*txt != '/')
            return NULL;
        /* FALLTHROUGH */

    case '/':
        if (txt[1] != '>')
            return NULL;
        *txt = NUL;
        txt += 2;
        load_opt_line(opts, ost, pzName, dir, mode);
        return txt;

    case '>':
        break;

    default:
        txt = strchr(txt, '>');
        if (txt != NULL)
            txt++;
        return txt;
    }

    /*
     *  If we are here, we have a value.  "txt" points to a closing angle
     *  bracket.  Separate the name from the value for a moment.
     */
    *pcNulPoint = NUL;
    pzData = ++txt;
    txt = trim_xml_text(txt, pzName, mode);
    if (txt == NULL)
        return txt;

    /*
     *  Rejoin the name and value for parsing by "load_opt_line()".
     *  Erase any attributes parsed by "parse_attrs()".
     */
    memset(pcNulPoint, ' ', (size_t)(pzData - pcNulPoint));

    /*
     *  If we are getting a "string" value that is to be cooked,
     *  then process the XML-ish &xx; XML-ish and %XX hex characters.
     */
    if (  (valu.valType == OPARG_TYPE_STRING)
       && (mode == OPTION_LOAD_COOKED))
        cook_xml_text(pzData);

    /*
     *  "pzName" points to what looks like text for one option/configurable.
     *  It is NUL terminated.  Process it.
     */
    load_opt_line(opts, ost, pzName, dir, mode);

    return txt;
}

/**
 *  Load a configuration file.  This may be invoked either from
 *  scanning the "homerc" list, or from a specific file request.
 *  (see "optionFileLoad()", the implementation for --load-opts)
 */
LOCAL void
intern_file_load(tOptions * opts)
{
    uint32_t  svfl;
    int       idx;
    int       inc;
    char      f_name[ AG_PATH_MAX+1 ];

    if (opts->papzHomeList == NULL)
        return;

    svfl = opts->fOptSet;
    inc  = DIRECTION_PRESET;

    /*
     *  Never stop on errors in config files.
     */
    opts->fOptSet &= ~OPTPROC_ERRSTOP;

    /*
     *  Find the last RC entry (highest priority entry)
     */
    for (idx = 0; opts->papzHomeList[ idx+1 ] != NULL; ++idx)  ;

    /*
     *  For every path in the home list, ...  *TWICE* We start at the last
     *  (highest priority) entry, work our way down to the lowest priority,
     *  handling the immediate options.
     *  Then we go back up, doing the normal options.
     */
    for (;;) {
        struct stat sb;
        cch_t *  path;

        /*
         *  IF we've reached the bottom end, change direction
         */
        if (idx < 0) {
            inc = DIRECTION_PROCESS;
            idx = 0;
        }

        path = opts->papzHomeList[ idx ];

        /*
         *  IF we've reached the top end, bail out
         */
        if (path == NULL)
            break;

        idx += inc;

        if (! optionMakePath(f_name, (int)sizeof(f_name),
                             path, opts->pzProgPath))
            continue;

        /*
         *  IF the file name we constructed is a directory,
         *  THEN append the Resource Configuration file name
         *  ELSE we must have the complete file name
         */
        if (stat(f_name, &sb) != 0)
            continue; /* bogus name - skip the home list entry */

        if (S_ISDIR(sb.st_mode)) {
            size_t len = strlen(f_name);
            size_t nln = strlen(opts->pzRcName) + 1;
            char * pz  = f_name + len;

            if (len + 1 + nln >= sizeof(f_name))
                continue;

            if (pz[-1] != DIRCH)
                *(pz++) = DIRCH;
            memcpy(pz, opts->pzRcName, nln);
        }

        file_preset(opts, f_name, inc);

        /*
         *  IF we are now to skip config files AND we are presetting,
         *  THEN change direction.  We must go the other way.
         */
        {
            tOptDesc * od = opts->pOptDesc + opts->specOptIdx.save_opts + 1;
            if (DISABLED_OPT(od) && PRESETTING(inc)) {
                idx -= inc;  /* go back and reprocess current file */
                inc =  DIRECTION_PROCESS;
            }
        }
    } /* twice for every path in the home list, ... */

    opts->fOptSet = svfl;
}

/*=export_func optionFileLoad
 *
 * what: Load the locatable config files, in order
 *
 * arg:  + tOptions *   + opts + program options descriptor +
 * arg:  + char const * + prog + program name +
 *
 * ret_type:  int
 * ret_desc:  0 -> SUCCESS, -1 -> FAILURE
 *
 * doc:
 *
 * This function looks in all the specified directories for a configuration
 * file ("rc" file or "ini" file) and processes any found twice.  The first
 * time through, they are processed in reverse order (last file first).  At
 * that time, only "immediate action" configurables are processed.  For
 * example, if the last named file specifies not processing any more
 * configuration files, then no more configuration files will be processed.
 * Such an option in the @strong{first} named directory will have no effect.
 *
 * Once the immediate action configurables have been handled, then the
 * directories are handled in normal, forward order.  In that way, later
 * config files can override the settings of earlier config files.
 *
 * See the AutoOpts documentation for a thorough discussion of the
 * config file format.
 *
 * Configuration files not found or not decipherable are simply ignored.
 *
 * err:  Returns the value, "-1" if the program options descriptor
 *       is out of date or indecipherable.  Otherwise, the value "0" will
 *       always be returned.
=*/
int
optionFileLoad(tOptions * opts, char const * prog)
{
    if (! SUCCESSFUL(validate_struct(opts, prog)))
        return -1;

    /*
     * The pointer to the program name is "const".  However, the
     * structure is in writable memory, so we coerce the address
     * of this pointer to point to writable memory.
     */
    {
        char const ** pp = VOIDP(&(opts->pzProgName));
        *pp = prog;
    }

    intern_file_load(opts);
    return 0;
}

/*=export_func  optionLoadOpt
 * private:
 *
 * what:  Load an option rc/ini file
 * arg:   + tOptions * + opts  + program options descriptor +
 * arg:   + tOptDesc * + odesc + the descriptor for this arg +
 *
 * doc:
 *  Processes the options found in the file named with
 *  odesc->optArg.argString.
=*/
void
optionLoadOpt(tOptions * opts, tOptDesc * odesc)
{
    struct stat sb;

    if (opts <= OPTPROC_EMIT_LIMIT)
        return;

    /*
     *  IF the option is not being disabled, THEN load the file.  There must
     *  be a file.  (If it is being disabled, then the disablement processing
     *  already took place.  It must be done to suppress preloading of ini/rc
     *  files.)
     */
    if (  DISABLED_OPT(odesc)
       || ((odesc->fOptState & OPTST_RESET) != 0))
        return;

    if (stat(odesc->optArg.argString, &sb) != 0) {
        if ((opts->fOptSet & OPTPROC_ERRSTOP) == 0)
            return;

        fserr_exit(opts->pzProgName, "stat", odesc->optArg.argString);
        /* NOT REACHED */
    }

    if (! S_ISREG(sb.st_mode)) {
        if ((opts->fOptSet & OPTPROC_ERRSTOP) == 0)
            return;
        errno = EINVAL;
        fserr_exit(opts->pzProgName, "stat", odesc->optArg.argString);
        /* NOT REACHED */
    }

    file_preset(opts, odesc->optArg.argString, DIRECTION_CALLED);
}

/**
 *  Parse the various attributes of an XML-styled config file entry
 *
 * @returns NULL on failure, otherwise the scan point
 */
LOCAL char const *
parse_attrs(tOptions * opts, char const * txt, tOptionLoadMode * pMode,
            tOptionValue * pType)
{
    size_t len = 0;

    for (;;) {
        len = (size_t)(SPN_LOWER_CASE_CHARS(txt) - txt);

        /*
         * The enumeration used in this switch is derived from this switch
         * statement itself.  The "find_option_xat_attribute_cmd" function
         * will return XAT_CMD_MEMBERS for the "txt" string value
         * "members", etc.
         */
        switch (find_option_xat_attribute_cmd(txt, len)) {
        case XAT_CMD_TYPE:
            txt = parse_value(txt+len, pType);
            break;

        case XAT_CMD_WORDS:
            txt = parse_keyword(opts, txt+len, pType);
            break;

        case XAT_CMD_MEMBERS:
            txt = parse_set_mem(opts, txt+len, pType);
            break;

        case XAT_CMD_COOKED:
            txt += len;
            if (! IS_END_XML_TOKEN_CHAR(*txt))
                goto invalid_kwd;

            *pMode = OPTION_LOAD_COOKED;
            break;

        case XAT_CMD_UNCOOKED:
            txt += len;
            if (! IS_END_XML_TOKEN_CHAR(*txt))
                goto invalid_kwd;

            *pMode = OPTION_LOAD_UNCOOKED;
            break;

        case XAT_CMD_KEEP:
            txt += len;
            if (! IS_END_XML_TOKEN_CHAR(*txt))
                goto invalid_kwd;

            *pMode = OPTION_LOAD_KEEP;
            break;

        default:
        case XAT_INVALID_CMD:
        invalid_kwd:
            pType->valType = OPARG_TYPE_NONE;
            return skip_unkn(txt);
        }

        if (txt == NULL)
            return NULL;
        txt = SPN_WHITESPACE_CHARS(txt);
        switch (*txt) {
            case '/': pType->valType = OPARG_TYPE_NONE;
                      /* FALLTHROUGH */
            case '>': return txt;
        }
        if (! IS_LOWER_CASE_CHAR(*txt))
            return NULL;
    }
}

/**
 *  "txt" points to the character after "words=".
 *  What should follow is a name of a keyword (enumeration) list.
 *
 *  @param     opts  unused
 *  @param[in] txt   keyword to skip over
 *  @param     type  unused value type
 *  @returns   pointer after skipped text
 */
static char const *
parse_keyword(tOptions * opts, char const * txt, tOptionValue * typ)
{
    (void)opts;
    (void)typ;

    return skip_unkn(txt);
}

/**
 *  "txt" points to the character after "members="
 *  What should follow is a name of a "set membership".
 *  A collection of bit flags.
 *
 *  @param     opts  unused
 *  @param[in] txt   keyword to skip over
 *  @param     type  unused value type
 *  @returns   pointer after skipped text
 */
static char const *
parse_set_mem(tOptions * opts, char const * txt, tOptionValue * typ)
{
    (void)opts;
    (void)typ;

    return skip_unkn(txt);
}

/**
 *  parse the type.  The keyword "type" was found, now figure out
 *  the type that follows the type.
 *
 *  @param[in]  txt  points to the '=' character after the "type" keyword.
 *  @param[out] typ  where to store the type found
 *  @returns    the next byte after the type name
 */
static char const *
parse_value(char const * txt, tOptionValue * typ)
{
    size_t len = 0;

    if (*(txt++) != '=')
        goto woops;

    len = (size_t)(SPN_OPTION_NAME_CHARS(txt) - txt);

    if ((len == 0) || (! IS_END_XML_TOKEN_CHAR(txt[len]))) {
    woops:
        typ->valType = OPARG_TYPE_NONE;
        return skip_unkn(txt + len);
    }

    /*
     * The enumeration used in this switch is derived from this switch
     * statement itself.  The "find_option_value_type_cmd" function
     * will return VTP_CMD_INTEGER for the "txt" string value
     * "integer", etc.
     */
    switch (find_option_value_type_cmd(txt, len)) {
    default:
    case VTP_INVALID_CMD: goto woops;

    case VTP_CMD_STRING:
        typ->valType = OPARG_TYPE_STRING;
        break;

    case VTP_CMD_INTEGER:
        typ->valType = OPARG_TYPE_NUMERIC;
        break;

    case VTP_CMD_BOOL:
    case VTP_CMD_BOOLEAN:
        typ->valType = OPARG_TYPE_BOOLEAN;
        break;

    case VTP_CMD_KEYWORD:
        typ->valType = OPARG_TYPE_ENUMERATION;
        break;

    case VTP_CMD_SET:
    case VTP_CMD_SET_MEMBERSHIP:
        typ->valType = OPARG_TYPE_MEMBERSHIP;
        break;

    case VTP_CMD_NESTED:
    case VTP_CMD_HIERARCHY:
        typ->valType = OPARG_TYPE_HIERARCHY;
    }

    return txt + len;
}

/** @}
 *
 * Local Variables:
 * mode: C
 * c-file-style: "stroustrup"
 * indent-tabs-mode: nil
 * End:
 * end of autoopts/configfile.c */
