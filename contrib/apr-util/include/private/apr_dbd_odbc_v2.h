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


/*  ONLY USED FOR ODBC Version 2   -DODBCV2
*
*   Re-define everything to work (more-or-less) in an ODBC V2 environment
*       Random access to retrieved rows is not supported - i.e. calls to apr_dbd_select() cannot
*       have a 'random' argument of 1.  apr_dbd_get_row() must always pass rownum as 0 (get next row)
*
*/

#define SQLHANDLE SQLHENV /* Presumes that ENV, DBC, and STMT handles are all the same datatype */
#define SQL_NULL_HANDLE 0
#define SQL_HANDLE_STMT 1
#define SQL_HANDLE_DBC  2
#define SQL_HANDLE_ENV  3
#define SQL_NO_DATA     SQL_NO_DATA_FOUND

#ifndef SQL_SUCCEEDED
#define SQL_SUCCEEDED(rc) (((rc)&(~1))==0)
#endif

#undef SQLSetEnvAttr
#define SQLSetEnvAttr(henv, Attribute, Value, StringLength)  (0)

#undef SQLAllocHandle
#define SQLAllocHandle(type, parent, hndl) \
(     (type == SQL_HANDLE_STMT) ? SQLAllocStmt(parent, hndl) \
    : (type == SQL_HANDLE_ENV)  ? SQLAllocEnv(hndl) \
    :                             SQLAllocConnect(parent, hndl)  \
)

#undef SQLFreeHandle
#define SQLFreeHandle(type, hndl) \
(     (type == SQL_HANDLE_STMT) ? SQLFreeStmt(hndl, SQL_DROP) \
    : (type == SQL_HANDLE_ENV)  ? SQLFreeEnv(hndl) \
    :                             SQLFreeConnect(hndl)  \
)

#undef SQLGetDiagRec
#define SQLGetDiagRec(type, h, i, state, native, buffer, bufsize, reslen) \
        SQLError(  (type == SQL_HANDLE_ENV) ? h : NULL, \
                   (type == SQL_HANDLE_DBC) ? h : NULL, \
                   (type == SQL_HANDLE_STMT) ? h : NULL, \
                   state, native, buffer, bufsize, reslen)

#undef SQLCloseCursor
#define SQLCloseCursor(stmt) SQLFreeStmt(stmt, SQL_CLOSE)

#undef SQLGetConnectAttr
#define SQLGetConnectAttr(hdbc, fOption, ValuePtr, BufferLength, NULL) \
    SQLGetConnectOption(hdbc, fOption, ValuePtr)

#undef SQLSetConnectAttr
#define SQLSetConnectAttr(hdbc, fOption, ValuePtr, BufferLength) \
        SQLSetConnectOption(hdbc, fOption, (SQLUINTEGER) ValuePtr)

#undef SQLSetStmtAttr
#define SQLSetStmtAttr(hstmt, fOption, ValuePtr, BufferLength) (0); return APR_ENOTIMPL;

#undef SQLEndTran
#define SQLEndTran(hType, hdbc,type)  SQLTransact(henv, hdbc, type)

#undef SQLFetchScroll
#define SQLFetchScroll(stmt, orient, rownum) (0); return APR_ENOTIMPL;

#define SQL_DESC_TYPE           SQL_COLUMN_TYPE
#define SQL_DESC_CONCISE_TYPE   SQL_COLUMN_TYPE
#define SQL_DESC_DISPLAY_SIZE   SQL_COLUMN_DISPLAY_SIZE
#define SQL_DESC_OCTET_LENGTH   SQL_COLUMN_LENGTH
#define SQL_DESC_UNSIGNED       SQL_COLUMN_UNSIGNED

#undef SQLColAttribute
#define SQLColAttribute(s, c, f, a, l, m, n) SQLColAttributes(s, c, f, a, l, m, n)

#define SQL_ATTR_ACCESS_MODE            SQL_ACCESS_MODE
#define SQL_ATTR_AUTOCOMMIT             SQL_AUTOCOMMIT
#define SQL_ATTR_CONNECTION_TIMEOUT     113
#define SQL_ATTR_CURRENT_CATALOG        SQL_CURRENT_QUALIFIER
#define SQL_ATTR_DISCONNECT_BEHAVIOR    114
#define SQL_ATTR_ENLIST_IN_DTC          1207
#define SQL_ATTR_ENLIST_IN_XA           1208

#define SQL_ATTR_CONNECTION_DEAD        1209
#define SQL_CD_TRUE                     1L   /* Connection is closed/dead */
#define SQL_CD_FALSE                    0L   /* Connection is open/available */

#define SQL_ATTR_LOGIN_TIMEOUT          SQL_LOGIN_TIMEOUT
#define SQL_ATTR_ODBC_CURSORS           SQL_ODBC_CURSORS
#define SQL_ATTR_PACKET_SIZE            SQL_PACKET_SIZE
#define SQL_ATTR_QUIET_MODE             SQL_QUIET_MODE
#define SQL_ATTR_TRACE                  SQL_OPT_TRACE
#define SQL_ATTR_TRACEFILE              SQL_OPT_TRACEFILE
#define SQL_ATTR_TRANSLATE_LIB          SQL_TRANSLATE_DLL
#define SQL_ATTR_TRANSLATE_OPTION       SQL_TRANSLATE_OPTION
#define SQL_ATTR_TXN_ISOLATION          SQL_TXN_ISOLATION

#define SQL_ATTR_CURSOR_SCROLLABLE -1

#define SQL_C_SBIGINT (SQL_BIGINT+SQL_SIGNED_OFFSET)   /* SIGNED BIGINT */
#define SQL_C_UBIGINT (SQL_BIGINT+SQL_UNSIGNED_OFFSET) /* UNSIGNED BIGINT */

#define SQL_FALSE           0
#define SQL_TRUE            1

