
/**
 * \file nested.c
 *
 *  Handle options with arguments that contain nested values.
 *
 * @addtogroup autoopts
 * @{
 */
/*
 *   Automated Options Nested Values module.
 *
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

typedef struct {
    int     xml_ch;
    int     xml_len;
    char    xml_txt[8];
} xml_xlate_t;

static xml_xlate_t const xml_xlate[] = {
    { '&', 4, "amp;"  },
    { '<', 3, "lt;"   },
    { '>', 3, "gt;"   },
    { '"', 5, "quot;" },
    { '\'',5, "apos;" }
};

#ifndef ENOMSG
#define ENOMSG ENOENT
#endif

/* = = = START-STATIC-FORWARD = = = */
static void
remove_continuation(char * src);

static char const *
scan_q_str(char const * pzTxt);

static tOptionValue *
add_string(void ** pp, char const * name, size_t nm_len,
           char const * val, size_t d_len);

static tOptionValue *
add_bool(void ** pp, char const * name, size_t nm_len,
         char const * val, size_t d_len);

static tOptionValue *
add_number(void ** pp, char const * name, size_t nm_len,
           char const * val, size_t d_len);

static tOptionValue *
add_nested(void ** pp, char const * name, size_t nm_len,
           char * val, size_t d_len);

static char const *
scan_name(char const * name, tOptionValue * res);

static char const *
unnamed_xml(char const * txt);

static char const *
scan_xml_name(char const * name, size_t * nm_len, tOptionValue * val);

static char const *
find_end_xml(char const * src, size_t nm_len, char const * val, size_t * len);

static char const *
scan_xml(char const * xml_name, tOptionValue * res_val);

static void
sort_list(tArgList * arg_list);
/* = = = END-STATIC-FORWARD = = = */

/**
 *  Backslashes are used for line continuations.  We keep the newline
 *  characters, but trim out the backslash:
 */
static void
remove_continuation(char * src)
{
    char * pzD;

    do  {
        while (*src == NL)  src++;
        pzD = strchr(src, NL);
        if (pzD == NULL)
            return;

        /*
         *  pzD has skipped at least one non-newline character and now
         *  points to a newline character.  It now becomes the source and
         *  pzD goes to the previous character.
         */
        src = pzD--;
        if (*pzD != '\\')
            pzD++;
    } while (pzD == src);

    /*
     *  Start shifting text.
     */
    for (;;) {
        char ch = ((*pzD++) = *(src++));
        switch (ch) {
        case NUL:  return;
        case '\\':
            if (*src == NL)
                --pzD; /* rewrite on next iteration */
        }
    }
}

/**
 *  Find the end of a quoted string, skipping escaped quote characters.
 */
static char const *
scan_q_str(char const * pzTxt)
{
    char q = *(pzTxt++); /* remember the type of quote */

    for (;;) {
        char ch = *(pzTxt++);
        if (ch == NUL)
            return pzTxt-1;

        if (ch == q)
            return pzTxt;

        if (ch == '\\') {
            ch = *(pzTxt++);
            /*
             *  IF the next character is NUL, drop the backslash, too.
             */
            if (ch == NUL)
                return pzTxt - 2;

            /*
             *  IF the quote character or the escape character were escaped,
             *  then skip both, as long as the string does not end.
             */
            if ((ch == q) || (ch == '\\')) {
                if (*(pzTxt++) == NUL)
                    return pzTxt-1;
            }
        }
    }
}


/**
 *  Associate a name with either a string or no value.
 *
 * @param[in,out] pp        argument list to add to
 * @param[in]     name      the name of the "suboption"
 * @param[in]     nm_len    the length of the name
 * @param[in]     val       the string value for the suboption
 * @param[in]     d_len     the length of the value
 *
 * @returns the new value structure
 */
static tOptionValue *
add_string(void ** pp, char const * name, size_t nm_len,
           char const * val, size_t d_len)
{
    tOptionValue * pNV;
    size_t sz = nm_len + d_len + sizeof(*pNV);

    pNV = AGALOC(sz, "option name/str value pair");

    if (val == NULL) {
        pNV->valType = OPARG_TYPE_NONE;
        pNV->pzName = pNV->v.strVal;

    } else {
        pNV->valType = OPARG_TYPE_STRING;
        if (d_len > 0) {
            char const * src = val;
            char * pzDst = pNV->v.strVal;
            int    ct    = (int)d_len;
            do  {
                int ch = *(src++) & 0xFF;
                if (ch == NUL) goto data_copy_done;
                if (ch == '&')
                    ch = get_special_char(&src, &ct);
                *(pzDst++) = (char)ch;
            } while (--ct > 0);
        data_copy_done:
            *pzDst = NUL;

        } else {
            pNV->v.strVal[0] = NUL;
        }

        pNV->pzName = pNV->v.strVal + d_len + 1;
    }

    memcpy(pNV->pzName, name, nm_len);
    pNV->pzName[ nm_len ] = NUL;
    addArgListEntry(pp, pNV);
    return pNV;
}

/**
 *  Associate a name with a boolean value
 *
 * @param[in,out] pp        argument list to add to
 * @param[in]     name      the name of the "suboption"
 * @param[in]     nm_len    the length of the name
 * @param[in]     val       the boolean value for the suboption
 * @param[in]     d_len     the length of the value
 *
 * @returns the new value structure
 */
static tOptionValue *
add_bool(void ** pp, char const * name, size_t nm_len,
         char const * val, size_t d_len)
{
    size_t sz = nm_len + sizeof(tOptionValue) + 1;
    tOptionValue * new_val = AGALOC(sz, "bool val");

    /*
     * Scan over whitespace is constrained by "d_len"
     */
    while (IS_WHITESPACE_CHAR(*val) && (d_len > 0)) {
        d_len--; val++;
    }

    if (d_len == 0)
        new_val->v.boolVal = 0;

    else if (IS_DEC_DIGIT_CHAR(*val))
        new_val->v.boolVal = (unsigned)atoi(val);

    else new_val->v.boolVal = ! IS_FALSE_TYPE_CHAR(*val);

    new_val->valType = OPARG_TYPE_BOOLEAN;
    new_val->pzName = (char *)(new_val + 1);
    memcpy(new_val->pzName, name, nm_len);
    new_val->pzName[ nm_len ] = NUL;
    addArgListEntry(pp, new_val);
    return new_val;
}

/**
 *  Associate a name with strtol() value, defaulting to zero.
 *
 * @param[in,out] pp        argument list to add to
 * @param[in]     name      the name of the "suboption"
 * @param[in]     nm_len    the length of the name
 * @param[in]     val       the numeric value for the suboption
 * @param[in]     d_len     the length of the value
 *
 * @returns the new value structure
 */
static tOptionValue *
add_number(void ** pp, char const * name, size_t nm_len,
           char const * val, size_t d_len)
{
    size_t sz = nm_len + sizeof(tOptionValue) + 1;
    tOptionValue * new_val = AGALOC(sz, "int val");

    /*
     * Scan over whitespace is constrained by "d_len"
     */
    while (IS_WHITESPACE_CHAR(*val) && (d_len > 0)) {
        d_len--; val++;
    }
    if (d_len == 0)
        new_val->v.longVal = 0;
    else
        new_val->v.longVal = strtol(val, 0, 0);

    new_val->valType = OPARG_TYPE_NUMERIC;
    new_val->pzName  = (char *)(new_val + 1);
    memcpy(new_val->pzName, name, nm_len);
    new_val->pzName[ nm_len ] = NUL;
    addArgListEntry(pp, new_val);
    return new_val;
}

/**
 *  Associate a name with a nested/hierarchical value.
 *
 * @param[in,out] pp        argument list to add to
 * @param[in]     name      the name of the "suboption"
 * @param[in]     nm_len    the length of the name
 * @param[in]     val       the nested values for the suboption
 * @param[in]     d_len     the length of the value
 *
 * @returns the new value structure
 */
static tOptionValue *
add_nested(void ** pp, char const * name, size_t nm_len,
           char * val, size_t d_len)
{
    tOptionValue * new_val;

    if (d_len == 0) {
        size_t sz = nm_len + sizeof(*new_val) + 1;
        new_val = AGALOC(sz, "empty nest");
        new_val->v.nestVal = NULL;
        new_val->valType = OPARG_TYPE_HIERARCHY;
        new_val->pzName = (char *)(new_val + 1);
        memcpy(new_val->pzName, name, nm_len);
        new_val->pzName[ nm_len ] = NUL;

    } else {
        new_val = optionLoadNested(val, name, nm_len);
    }

    if (new_val != NULL)
        addArgListEntry(pp, new_val);

    return new_val;
}

/**
 *  We have an entry that starts with a name.  Find the end of it, cook it
 *  (if called for) and create the name/value association.
 */
static char const *
scan_name(char const * name, tOptionValue * res)
{
    tOptionValue * new_val;
    char const *   pzScan = name+1; /* we know first char is a name char */
    char const *   pzVal;
    size_t         nm_len = 1;
    size_t         d_len = 0;

    /*
     *  Scan over characters that name a value.  These names may not end
     *  with a colon, but they may contain colons.
     */
    pzScan = SPN_VALUE_NAME_CHARS(name + 1);
    if (pzScan[-1] == ':')
        pzScan--;
    nm_len = (size_t)(pzScan - name);

    pzScan = SPN_HORIZ_WHITE_CHARS(pzScan);

 re_switch:

    switch (*pzScan) {
    case '=':
    case ':':
        pzScan = SPN_HORIZ_WHITE_CHARS(pzScan + 1);
        if ((*pzScan == '=') || (*pzScan == ':'))
            goto default_char;
        goto re_switch;

    case NL:
    case ',':
        pzScan++;
        /* FALLTHROUGH */

    case NUL:
        add_string(&(res->v.nestVal), name, nm_len, NULL, (size_t)0);
        break;

    case '"':
    case '\'':
        pzVal = pzScan;
        pzScan = scan_q_str(pzScan);
        d_len = (size_t)(pzScan - pzVal);
        new_val = add_string(&(res->v.nestVal), name, nm_len, pzVal,
                         d_len);
        if ((new_val != NULL) && (option_load_mode == OPTION_LOAD_COOKED))
            ao_string_cook(new_val->v.strVal, NULL);
        break;

    default:
    default_char:
        /*
         *  We have found some strange text value.  It ends with a newline
         *  or a comma.
         */
        pzVal = pzScan;
        for (;;) {
            char ch = *(pzScan++);
            switch (ch) {
            case NUL:
                pzScan--;
                d_len = (size_t)(pzScan - pzVal);
                goto string_done;
                /* FALLTHROUGH */

            case NL:
                if (   (pzScan > pzVal + 2)
                    && (pzScan[-2] == '\\')
                    && (pzScan[ 0] != NUL))
                    continue;
                /* FALLTHROUGH */

            case ',':
                d_len = (size_t)(pzScan - pzVal) - 1;
            string_done:
                new_val = add_string(&(res->v.nestVal), name, nm_len,
                                     pzVal, d_len);
                if (new_val != NULL)
                    remove_continuation(new_val->v.strVal);
                goto leave_scan_name;
            }
        }
        break;
    } leave_scan_name:;

    return pzScan;
}

/**
 * Some xml element that does not start with a name.
 * The next character must be either '!' (introducing a comment),
 * or '?' (introducing an XML meta-marker of some sort).
 * We ignore these and indicate an error (NULL result) otherwise.
 *
 * @param[in] txt  the text within an xml bracket
 * @returns the address of the character after the closing marker, or NULL.
 */
static char const *
unnamed_xml(char const * txt)
{
    switch (*txt) {
    default:
        txt = NULL;
        break;

    case '!':
        txt = strstr(txt, "-->");
        if (txt != NULL)
            txt += 3;
        break;

    case '?':
        txt = strchr(txt, '>');
        if (txt != NULL)
            txt++;
        break;
    }
    return txt;
}

/**
 *  Scan off the xml element name, and the rest of the header, too.
 *  Set the value type to NONE if it ends with "/>".
 *
 * @param[in]  name    the first name character (alphabetic)
 * @param[out] nm_len  the length of the name
 * @param[out] val     set valType field to STRING or NONE.
 *
 * @returns the scan resumption point, or NULL on error
 */
static char const *
scan_xml_name(char const * name, size_t * nm_len, tOptionValue * val)
{
    char const * scan = SPN_VALUE_NAME_CHARS(name + 1);
    *nm_len = (size_t)(scan - name);
    if (*nm_len > 64)
        return NULL;
    val->valType = OPARG_TYPE_STRING;

    if (IS_WHITESPACE_CHAR(*scan)) {
        /*
         * There are attributes following the name.  Parse 'em.
         */
        scan = SPN_WHITESPACE_CHARS(scan);
        scan = parse_attrs(NULL, scan, &option_load_mode, val);
        if (scan == NULL)
            return NULL; /* oops */
    }

    if (! IS_END_XML_TOKEN_CHAR(*scan))
        return NULL; /* oops */

    if (*scan == '/') {
        /*
         * Single element XML entries get inserted as an empty string.
         */
        if (*++scan != '>')
            return NULL;
        val->valType = OPARG_TYPE_NONE;
    }
    return scan+1;
}

/**
 * We've found a closing '>' without a preceding '/', thus we must search
 * the text for '<name/>' where "name" is the name of the XML element.
 *
 * @param[in]  name     the start of the name in the element header
 * @param[in]  nm_len   the length of that name
 * @param[out] len      the length of the value (string between header and
 *                      the trailer/tail.
 * @returns the character after the trailer, or NULL if not found.
 */
static char const *
find_end_xml(char const * src, size_t nm_len, char const * val, size_t * len)
{
    char z[72] = "</";
    char * dst = z + 2;

    do  {
        *(dst++) = *(src++);
    } while (--nm_len > 0); /* nm_len is known to be 64 or less */
    *(dst++) = '>';
    *dst = NUL;

    {
        char const * res = strstr(val, z);

        if (res != NULL) {
            char const * end = (option_load_mode != OPTION_LOAD_KEEP)
                ? SPN_WHITESPACE_BACK(val, res)
                : res;
            *len = (size_t)(end - val); /* includes trailing white space */
            res =  SPN_WHITESPACE_CHARS(res + (dst - z));
        }
        return res;
    }
}

/**
 *  We've found a '<' character.  We ignore this if it is a comment or a
 *  directive.  If it is something else, then whatever it is we are looking
 *  at is bogus.  Returning NULL stops processing.
 *
 * @param[in]     xml_name  the name of an xml bracket (usually)
 * @param[in,out] res_val   the option data derived from the XML element
 *
 * @returns the place to resume scanning input
 */
static char const *
scan_xml(char const * xml_name, tOptionValue * res_val)
{
    size_t          nm_len, v_len;
    char const *    scan;
    char const *    val_str;
    tOptionValue    valu;
    tOptionLoadMode save_mode = option_load_mode;

    if (! IS_VAR_FIRST_CHAR(*++xml_name))
        return unnamed_xml(xml_name);

    /*
     * "scan_xml_name()" may change "option_load_mode".
     */
    val_str = scan_xml_name(xml_name, &nm_len, &valu);
    if (val_str == NULL)
        goto bail_scan_xml;

    if (valu.valType == OPARG_TYPE_NONE)
        scan = val_str;
    else {
        if (option_load_mode != OPTION_LOAD_KEEP)
            val_str = SPN_WHITESPACE_CHARS(val_str);
        scan = find_end_xml(xml_name, nm_len, val_str, &v_len);
        if (scan == NULL)
            goto bail_scan_xml;
    }

    /*
     * "scan" now points to where the scan is to resume after returning.
     * It either points after "/>" at the end of the XML element header,
     * or it points after the "</name>" tail based on the name in the header.
     */

    switch (valu.valType) {
    case OPARG_TYPE_NONE:
        add_string(&(res_val->v.nestVal), xml_name, nm_len, NULL, 0);
        break;

    case OPARG_TYPE_STRING:
    {
        tOptionValue * new_val = add_string(
            &(res_val->v.nestVal), xml_name, nm_len, val_str, v_len);

        if (option_load_mode != OPTION_LOAD_KEEP)
            munge_str(new_val->v.strVal, option_load_mode);

        break;
    }

    case OPARG_TYPE_BOOLEAN:
        add_bool(&(res_val->v.nestVal), xml_name, nm_len, val_str, v_len);
        break;

    case OPARG_TYPE_NUMERIC:
        add_number(&(res_val->v.nestVal), xml_name, nm_len, val_str, v_len);
        break;

    case OPARG_TYPE_HIERARCHY:
    {
        char * pz = AGALOC(v_len+1, "h scan");
        memcpy(pz, val_str, v_len);
        pz[v_len] = NUL;
        add_nested(&(res_val->v.nestVal), xml_name, nm_len, pz, v_len);
        AGFREE(pz);
        break;
    }

    case OPARG_TYPE_ENUMERATION:
    case OPARG_TYPE_MEMBERSHIP:
    default:
        break;
    }

    option_load_mode = save_mode;
    return scan;

bail_scan_xml:
    option_load_mode = save_mode;
    return NULL;
}


/**
 *  Deallocate a list of option arguments.  This must have been gotten from
 *  a hierarchical option argument, not a stacked list of strings.  It is
 *  an internal call, so it is not validated.  The caller is responsible for
 *  knowing what they are doing.
 */
LOCAL void
unload_arg_list(tArgList * arg_list)
{
    int ct = arg_list->useCt;
    char const ** pnew_val = arg_list->apzArgs;

    while (ct-- > 0) {
        tOptionValue * new_val = (tOptionValue *)VOIDP(*(pnew_val++));
        if (new_val->valType == OPARG_TYPE_HIERARCHY)
            unload_arg_list(new_val->v.nestVal);
        AGFREE(new_val);
    }

    AGFREE(arg_list);
}

/*=export_func  optionUnloadNested
 *
 * what:  Deallocate the memory for a nested value
 * arg:   + tOptionValue const * + pOptVal + the hierarchical value +
 *
 * doc:
 *  A nested value needs to be deallocated.  The pointer passed in should
 *  have been gotten from a call to @code{configFileLoad()} (See
 *  @pxref{libopts-configFileLoad}).
=*/
void
optionUnloadNested(tOptionValue const * opt_val)
{
    if (opt_val == NULL) return;
    if (opt_val->valType != OPARG_TYPE_HIERARCHY) {
        errno = EINVAL;
        return;
    }

    unload_arg_list(opt_val->v.nestVal);

    AGFREE(opt_val);
}

/**
 *  This is a _stable_ sort.  The entries are sorted alphabetically,
 *  but within entries of the same name the ordering is unchanged.
 *  Typically, we also hope the input is sorted.
 */
static void
sort_list(tArgList * arg_list)
{
    int ix;
    int lm = arg_list->useCt;

    /*
     *  This loop iterates "useCt" - 1 times.
     */
    for (ix = 0; ++ix < lm;) {
        int iy = ix-1;
        tOptionValue * new_v = C(tOptionValue *, arg_list->apzArgs[ix]);
        tOptionValue * old_v = C(tOptionValue *, arg_list->apzArgs[iy]);

        /*
         *  For as long as the new entry precedes the "old" entry,
         *  move the old pointer.  Stop before trying to extract the
         *  "-1" entry.
         */
        while (strcmp(old_v->pzName, new_v->pzName) > 0) {
            arg_list->apzArgs[iy+1] = VOIDP(old_v);
            old_v = (tOptionValue *)VOIDP(arg_list->apzArgs[--iy]);
            if (iy < 0)
                break;
        }

        /*
         *  Always store the pointer.  Sometimes it is redundant,
         *  but the redundancy is cheaper than a test and branch sequence.
         */
        arg_list->apzArgs[iy+1] = VOIDP(new_v);
    }
}

/*=
 * private:
 *
 * what:  parse a hierarchical option argument
 * arg:   + char const * + pzTxt  + the text to scan      +
 * arg:   + char const * + pzName + the name for the text +
 * arg:   + size_t       + nm_len + the length of "name"  +
 *
 * ret_type:  tOptionValue *
 * ret_desc:  An allocated, compound value structure
 *
 * doc:
 *  A block of text represents a series of values.  It may be an
 *  entire configuration file, or it may be an argument to an
 *  option that takes a hierarchical value.
 *
 *  If NULL is returned, errno will be set:
 *  @itemize @bullet
 *  @item
 *  @code{EINVAL} the input text was NULL.
 *  @item
 *  @code{ENOMEM} the storage structures could not be allocated
 *  @item
 *  @code{ENOMSG} no configuration values were found
 *  @end itemize
=*/
LOCAL tOptionValue *
optionLoadNested(char const * text, char const * name, size_t nm_len)
{
    tOptionValue * res_val;

    /*
     *  Make sure we have some data and we have space to put what we find.
     */
    if (text == NULL) {
        errno = EINVAL;
        return NULL;
    }
    text = SPN_WHITESPACE_CHARS(text);
    if (*text == NUL) {
        errno = ENOMSG;
        return NULL;
    }
    res_val = AGALOC(sizeof(*res_val) + nm_len + 1, "nest args");
    res_val->valType = OPARG_TYPE_HIERARCHY;
    res_val->pzName  = (char *)(res_val + 1);
    memcpy(res_val->pzName, name, nm_len);
    res_val->pzName[nm_len] = NUL;

    {
        tArgList * arg_list = AGALOC(sizeof(*arg_list), "nest arg l");

        res_val->v.nestVal = arg_list;
        arg_list->useCt   = 0;
        arg_list->allocCt = MIN_ARG_ALLOC_CT;
    }

    /*
     *  Scan until we hit a NUL.
     */
    do  {
        text = SPN_WHITESPACE_CHARS(text);
        if (IS_VAR_FIRST_CHAR(*text))
            text = scan_name(text, res_val);

        else switch (*text) {
        case NUL: goto scan_done;
        case '<': text = scan_xml(text, res_val);
                  if (text == NULL) goto woops;
                  if (*text == ',') text++; break;
        case '#': text = strchr(text, NL);  break;
        default:  goto woops;
        }
    } while (text != NULL); scan_done:;

    {
        tArgList * al = res_val->v.nestVal;
        if (al->useCt == 0) {
            errno = ENOMSG;
            goto woops;
        }
        if (al->useCt > 1)
            sort_list(al);
    }

    return res_val;

 woops:
    AGFREE(res_val->v.nestVal);
    AGFREE(res_val);
    return NULL;
}

/*=export_func  optionNestedVal
 * private:
 *
 * what:  parse a hierarchical option argument
 * arg:   + tOptions * + opts + program options descriptor +
 * arg:   + tOptDesc * + od   + the descriptor for this arg +
 *
 * doc:
 *  Nested value was found on the command line
=*/
void
optionNestedVal(tOptions * opts, tOptDesc * od)
{
    if (opts < OPTPROC_EMIT_LIMIT)
        return;

    if (od->fOptState & OPTST_RESET) {
        tArgList *    arg_list = od->optCookie;
        int           ct;
        char const ** av;

        if (arg_list == NULL)
            return;
        ct = arg_list->useCt;
        av = arg_list->apzArgs;

        while (--ct >= 0) {
            void * p = VOIDP(*(av++));
            optionUnloadNested((tOptionValue const *)p);
        }

        AGFREE(od->optCookie);

    } else {
        tOptionValue * opt_val = optionLoadNested(
            od->optArg.argString, od->pz_Name, strlen(od->pz_Name));

        if (opt_val != NULL)
            addArgListEntry(&(od->optCookie), VOIDP(opt_val));
    }
}

/**
 * get_special_char
 */
LOCAL int
get_special_char(char const ** ppz, int * ct)
{
    char const * pz = *ppz;
    char *       rz;

    if (*ct < 3)
        return '&';

    if (*pz == '#') {
        int base = 10;
        int retch;

        pz++;
        if (*pz == 'x') {
            base = 16;
            pz++;
        }
        retch = (int)strtoul(pz, &rz, base);
        pz = rz;
        if (*pz != ';')
            return '&';
        base = (int)(++pz - *ppz);
        if (base > *ct)
            return '&';

        *ct -= base;
        *ppz = pz;
        return retch;
    }

    {
        int ctr = sizeof(xml_xlate) / sizeof(xml_xlate[0]);
        xml_xlate_t const * xlatp = xml_xlate;

        for (;;) {
            if (  (*ct >= xlatp->xml_len)
               && (strncmp(pz, xlatp->xml_txt, (size_t)xlatp->xml_len) == 0)) {
                *ppz += xlatp->xml_len;
                *ct  -= xlatp->xml_len;
                return xlatp->xml_ch;
            }

            if (--ctr <= 0)
                break;
            xlatp++;
        }
    }
    return '&';
}

/**
 * emit_special_char
 */
LOCAL void
emit_special_char(FILE * fp, int ch)
{
    int ctr = sizeof(xml_xlate) / sizeof(xml_xlate[0]);
    xml_xlate_t const * xlatp = xml_xlate;

    putc('&', fp);
    for (;;) {
        if (ch == xlatp->xml_ch) {
            fputs(xlatp->xml_txt, fp);
            return;
        }
        if (--ctr <= 0)
            break;
        xlatp++;
    }
    fprintf(fp, XML_HEX_BYTE_FMT, (ch & 0xFF));
}

/** @}
 *
 * Local Variables:
 * mode: C
 * c-file-style: "stroustrup"
 * indent-tabs-mode: nil
 * End:
 * end of autoopts/nested.c */
