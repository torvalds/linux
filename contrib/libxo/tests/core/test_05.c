/*
 * Copyright (c) 2014, Juniper Networks, Inc.
 * All rights reserved.
 * This SOFTWARE is licensed under the LICENSE provided in the
 * ../Copyright file. By downloading, installing, copying, or otherwise
 * using the SOFTWARE, you agree to be bound by the terms of that
 * LICENSE.
 * Phil Shafer, July 2014
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "xo.h"
#include "xo_config.h"

#ifdef LIBXO_WCWIDTH
#include "xo_wcwidth.h"
#else /* LIBXO_WCWIDTH */
#define xo_wcwidth(_x) wcwidth(_x)
#endif /* LIBXO_WCWIDTH */

xo_info_t info[] = {
    { "employee", "object", "Employee data" },
    { "first-name", "string", "First name of employee" },
    { "last-name", "string", "Last name of employee" },
    { "department", "number", "Department number" },
    { "percent-time", "number", "Percentage of full & part time (%)" },
};
int info_count = (sizeof(info) / sizeof(info[0]));

int
main (int argc, char **argv)
{
    struct employee {
	const char *e_first;
	const char *e_nic;
	const char *e_last;
	unsigned e_dept;
	unsigned e_percent;
    } employees[] = {
	{ "Jim", "რეგტ", "გთხოვთ ახ", 431, 90 },
	{ "Terry", "<one", "Οὐχὶ ταὐτὰ παρίσταταί μοι Jones", 660, 90 },
	{ "Leslie", "Les", "Patterson", 341,60 },
	{ "Ashley", "Ash", "Meter & Smith", 1440, 40 },
	{ "0123456789", "0123456789", "012345678901234567890", 1440, 40 },
	{ "ახლა", "გაიარო", "საერთაშორისო", 123, 90 },
	{ "෴ණ්ණ෴෴ණ්ණ෴", "Mick",
	  "෴ණ්ණ෴෴ණ්ණ෴෴ණ්ණ෴෴෴", 110, 20 },
	{ NULL, NULL }
    }, *ep = employees;
    int rc, i;

    argc = xo_parse_args(argc, argv);
    if (argc < 0)
	return 1;

    xo_set_info(NULL, info, info_count);
    xo_set_flags(NULL, XOF_COLUMNS);

    xo_open_container("indian-languages");
    
    xo_emit("{T:Sample text}\n");
    xo_emit("This sample text was taken from the Punjabi Wikipedia "
	    "article on Lahore and transliterated into the Latin script.\n");

    xo_emit("{T:Gurmukhi:}\n");
    xo_emit("{:gurmukhi}\n",
	    "ਲਹੌਰ ਪਾਕਿਸਤਾਨੀ ਪੰਜਾਬ ਦੀ ਰਾਜਧਾਨੀ ਹੈ । ਲੋਕ ਗਿਣਤੀ ਦੇ ਨਾਲ ਕਰਾਚੀ ਤੋਂ ਬਾਅਦ ਲਹੌਰ ਦੂਜਾ ਸਭ ਤੋਂ ਵੱਡਾ ਸ਼ਹਿਰ ਹੈ । ਲਹੌਰ ਪਾਕਿਸਤਾਨ ਦਾ ਸਿਆਸੀ, ਰਹਤਲੀ ਤੇ ਪੜ੍ਹਾਈ ਦਾ ਗੜ੍ਹ ਹੈ ਅਤੇ ਇਸ ਲਈ ਇਹਨੂੰ ਪਾਕਿਸਤਾਨ ਦਾ ਦਿਲ ਵੀ ਕਿਹਾ ਜਾਂਦਾ ਹੈ । ਲਹੌਰ ਦਰਿਆ-ਏ-ਰਾਵੀ ਦੇ ਕੰਢੇ ਤੇ ਵਸਦਾ ਹੈ ਤੇ ਇਸਦੀ ਲੋਕ ਗਿਣਤੀ ਇੱਕ ਕਰੋੜ ਦੇ ਨੇੜੇ ਹੈ ।");


    xo_emit("{T:Shahmukhi:}\n");
    xo_emit("{:shahmukhi}\n",
	    "لہور پاکستانی پنجاب دا دارالحکومت اے۔ لوک گنتی دے نال کراچی توں بعد لہور دوجا سبھ توں وڈا شہر اے۔ لہور پاکستان دا سیاسی، رہتلی تے پڑھائی دا گڑھ اے تے اس لئی ایھنوں پاکستان دا دل وی کیھا جاندا اے۔ لہور دریاۓ راوی دے کنڈھے تے وسدا اے اسدی لوک گنتی اک کروڑ دے نیڑے اے ۔");

    xo_emit("{T:Transliteration}:\n");
    xo_emit("{:tranliteration}\n",
	    "lahor pākistān panjāb dā dārul hakūmat ē. lōk giṇtī dē nāḷ karācī tō᷈ bāad lahor dūjā sab tō᷈ vaḍḍā shahr ē. lahor pākistān dā siāsī, rahtalī tē paṛā̀ī dā gā́ṛ ē tē is laī ihnū᷈ pākistān dā dil vī kehā jāndā ē. lahor dariāē rāvī dē kanḍē tē vasdā ē. isdī lōk giṇtī ikk karōṛ dē nēṛē ē.");

    xo_close_container("indian-languages");

    xo_open_container("employees");

    wchar_t wc[] = { L'෴', L'ණ', L'්', L'ණ', 0x17D2, L'෴', 0 };
    for (i = 0; wc[i]; i++)
	xo_emit("Wide char: {lq:wc/%lc - %#lx - %d}\n",
		wc[i], (unsigned long) wc[i], xo_wcwidth(wc[i]));

    wchar_t msg[] = { L'1', 0x034f, L'2', 0x20dd, 0 };
    for (i = 0; msg[i]; i++)
	xo_emit("Wide char: {lq:wc/%lc - %#lx - %d}\n",
		msg[i], (unsigned long) msg[i], xo_wcwidth((int) msg[i]));
    xo_emit("Cool: [{:fancy/%ls}]\n", msg);

    xo_emit("Οὐχὶ ταὐτὰ παρίσταταί μοι {:v1/%s}, {:v2/%s}\n",
	    "γιγνώσκειν", "ὦ ἄνδρες ᾿Αθηναῖοι");

    rc = xo_emit("გთხოვთ {:v1/%s} {:v2/%s}\n",
	    "ახლავე გაიაროთ რეგისტრაცია",
	    "Unicode-ის მეათე საერთაშორისო");
    xo_emit("{Twc:Width}{:width/%d}\n", rc);

    /* Okay, Sinhala is uber cool ... */
    rc = xo_emit("[{:sinhala}]\n", "෴ණ්ණ෴");
    xo_emit("{Twc:Width}{:width/%d}\n", rc);

    rc = xo_emit("[{:sinhala}]\n", "෴");
    xo_emit("{Twc:Width}{:width/%d}\n", rc);

    rc = xo_emit("[{:sinhala/%-4..4s/%s}]\n", "෴ණ්ණ෴෴ණ්ණ෴");
    xo_emit("{Twc:Width}{:width/%d}\n", rc);

    xo_emit("[{:not-sinhala/%-4..4s/%s}]\n", "123456");

    rc = xo_emit("[{:tag/%s}]\n", "ර්‍ඝ");
    xo_emit("{Twc:Width}{:width/%d}\n", rc);

    xo_open_list("employee");

    xo_emit("{T:First Name/%-25s}{T:Last Name/%-14s}"
	    "{T:/%-12s}{T:Time (%)}\n", "Department");
    for ( ; ep->e_first; ep++) {
	xo_open_instance("employee");
	xo_emit("{[:-25}{:first-name/%s} ({:nic-name/\"%s\"}){]:}"
		"{:last-name/%-14..14s/%s}"
		"{:department/%8u}{:percent-time/%8u}\n",
		ep->e_first, ep->e_nic, ep->e_last, ep->e_dept, ep->e_percent);
	if (ep->e_percent > 50) {
	    xo_attr("full-time", "%s", "honest & for true");
	    xo_emit("{e:benefits/%s}", "full");
	}
	xo_close_instance("employee");
    }

    xo_close_list("employee");
    xo_close_container("employees");

    xo_finish();

    return 0;
}
