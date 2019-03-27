/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "apr.h"
#include "apr_general.h"
#include "apr_xml.h"
#include "abts.h"
#include "testutil.h"

static apr_status_t create_dummy_file_error(abts_case *tc, apr_pool_t *p,
                                            apr_file_t **fd)
{
    int i;
    apr_status_t rv;
    apr_off_t off = 0L;
    char template[] = "data/testxmldummyerrorXXXXXX";

    rv = apr_file_mktemp(fd, template, APR_FOPEN_CREATE | APR_FOPEN_TRUNCATE | APR_FOPEN_DELONCLOSE |
                         APR_FOPEN_READ | APR_FOPEN_WRITE | APR_FOPEN_EXCL, p);
    ABTS_INT_EQUAL(tc, APR_SUCCESS, rv);

    if (rv != APR_SUCCESS)
        return rv;

    rv = apr_file_puts("<?xml version=\"1.0\" ?>\n<maryx>"
                       "<had a=\"little\"/><lamb/>\n", *fd);
    ABTS_INT_EQUAL(tc, APR_SUCCESS, rv);

    for (i = 0; i < 5000; i++) {
        rv = apr_file_puts("<hmm roast=\"lamb\" "
                           "for=\"dinner\">yummy</hmm>\n", *fd);
        ABTS_INT_EQUAL(tc, APR_SUCCESS, rv);
    }

    rv = apr_file_puts("</mary>\n", *fd);
    ABTS_INT_EQUAL(tc, APR_SUCCESS, rv);

    rv = apr_file_seek(*fd, APR_SET, &off);
    ABTS_INT_EQUAL(tc, APR_SUCCESS, rv);

    return rv;
}

static apr_status_t create_dummy_file(abts_case *tc, apr_pool_t *p,
                                      apr_file_t **fd)
{
    int i;
    apr_status_t rv;
    apr_off_t off = 0L;
    char template[] = "data/testxmldummyXXXXXX";

    rv = apr_file_mktemp(fd, template, APR_FOPEN_CREATE | APR_FOPEN_TRUNCATE | APR_FOPEN_DELONCLOSE |
                         APR_FOPEN_READ | APR_FOPEN_WRITE | APR_FOPEN_EXCL, p);
    ABTS_INT_EQUAL(tc, APR_SUCCESS, rv);

    if (rv != APR_SUCCESS)
        return rv;

    rv = apr_file_puts("<?xml version=\"1.0\" ?>\n<mary>\n", *fd);
    ABTS_INT_EQUAL(tc, APR_SUCCESS, rv);

    for (i = 0; i < 5000; i++) {
        rv = apr_file_puts("<hmm roast=\"lamb\" "
                           "for=\"dinner &lt;&gt;&#x3D;\">yummy</hmm>\n", *fd);
        ABTS_INT_EQUAL(tc, APR_SUCCESS, rv);
    }

    rv = apr_file_puts("</mary>\n", *fd);
    ABTS_INT_EQUAL(tc, APR_SUCCESS, rv);

    rv = apr_file_seek(*fd, APR_SET, &off);
    ABTS_INT_EQUAL(tc, APR_SUCCESS, rv);

    return rv;
}

static void dump_xml(abts_case *tc, apr_xml_elem *e, int level)
{
    apr_xml_attr *a;
    apr_xml_elem *ec;

    if (level == 0) {
        ABTS_STR_EQUAL(tc, "mary", e->name);
    } else {
        ABTS_STR_EQUAL(tc, "hmm", e->name);
    }

    if (e->attr) {
        a = e->attr;
        ABTS_PTR_NOTNULL(tc, a);
        ABTS_STR_EQUAL(tc, "for", a->name);
        ABTS_STR_EQUAL(tc, "dinner <>=", a->value);
        a = a->next;
        ABTS_PTR_NOTNULL(tc, a);
        ABTS_STR_EQUAL(tc, "roast", a->name);
        ABTS_STR_EQUAL(tc, "lamb", a->value);
    }
    if (e->first_child) {
        ec = e->first_child;
        while (ec) {
            dump_xml(tc, ec, level + 1);
            ec = ec->next;
        }
    }
}

static void test_xml_parser(abts_case *tc, void *data)
{
    apr_file_t *fd;
    apr_xml_parser *parser;
    apr_xml_doc *doc;
    apr_status_t rv;

    rv = create_dummy_file(tc, p, &fd);
    ABTS_INT_EQUAL(tc, APR_SUCCESS, rv);

    if (rv != APR_SUCCESS)
        return;

    rv = apr_xml_parse_file(p, &parser, &doc, fd, 2000);
    ABTS_INT_EQUAL(tc, APR_SUCCESS, rv);

    dump_xml(tc, doc->root, 0);

    rv = apr_file_close(fd);
    ABTS_INT_EQUAL(tc, APR_SUCCESS, rv);

    rv = create_dummy_file_error(tc, p, &fd);
    ABTS_INT_EQUAL(tc, APR_SUCCESS, rv);

    if (rv != APR_SUCCESS)
        return;

    rv = apr_xml_parse_file(p, &parser, &doc, fd, 2000);
    ABTS_TRUE(tc, rv != APR_SUCCESS);
}

static void test_billion_laughs(abts_case *tc, void *data)
{
    apr_file_t *fd;
    apr_xml_parser *parser;
    apr_xml_doc *doc;
    apr_status_t rv;

    rv = apr_file_open(&fd, "data/billion-laughs.xml", 
                       APR_FOPEN_READ, 0, p);
    apr_assert_success(tc, "open billion-laughs.xml", rv);

    /* Don't test for return value; if it returns, chances are the bug
     * is fixed or the machine has insane amounts of RAM. */
    apr_xml_parse_file(p, &parser, &doc, fd, 2000);

    apr_file_close(fd);
}

static void test_CVE_2009_3720_alpha(abts_case *tc, void *data)
{
    apr_xml_parser *xp;
    apr_xml_doc *doc;
    apr_status_t rv;

    xp = apr_xml_parser_create(p);
    
    rv = apr_xml_parser_feed(xp, "\0\r\n", 3);
    if (rv == APR_SUCCESS)
        apr_xml_parser_done(xp, &doc);
}

static void test_CVE_2009_3720_beta(abts_case *tc, void *data)
{
    apr_xml_parser *xp;
    apr_xml_doc *doc;
    apr_status_t rv;

    xp = apr_xml_parser_create(p);
    
    rv = apr_xml_parser_feed(xp, "<?xml version\xc2\x85='1.0'?>\r\n", 25);
    if (rv == APR_SUCCESS)
        apr_xml_parser_done(xp, &doc);
}

abts_suite *testxml(abts_suite *suite)
{
    suite = ADD_SUITE(suite);

    abts_run_test(suite, test_xml_parser, NULL);
    abts_run_test(suite, test_billion_laughs, NULL);
    abts_run_test(suite, test_CVE_2009_3720_alpha, NULL);
    abts_run_test(suite, test_CVE_2009_3720_beta, NULL);

    return suite;
}
