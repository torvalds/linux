/*
                            __  __            _
                         ___\ \/ /_ __   __ _| |_
                        / _ \\  /| '_ \ / _` | __|
                       |  __//  \| |_) | (_| | |_
                        \___/_/\_\ .__/ \__,_|\__|
                                 |_| XML parser

   Copyright (c) 1997-2000 Thai Open Source Software Center Ltd
   Copyright (c) 2000-2017 Expat development team
   Licensed under the MIT license:

   Permission is  hereby granted,  free of charge,  to any  person obtaining
   a  copy  of  this  software   and  associated  documentation  files  (the
   "Software"),  to  deal in  the  Software  without restriction,  including
   without  limitation the  rights  to use,  copy,  modify, merge,  publish,
   distribute, sublicense, and/or sell copies of the Software, and to permit
   persons  to whom  the Software  is  furnished to  do so,  subject to  the
   following conditions:

   The above copyright  notice and this permission notice  shall be included
   in all copies or substantial portions of the Software.

   THE  SOFTWARE  IS  PROVIDED  "AS  IS",  WITHOUT  WARRANTY  OF  ANY  KIND,
   EXPRESS  OR IMPLIED,  INCLUDING  BUT  NOT LIMITED  TO  THE WARRANTIES  OF
   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
   NO EVENT SHALL THE AUTHORS OR  COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
   DAMAGES OR  OTHER LIABILITY, WHETHER  IN AN  ACTION OF CONTRACT,  TORT OR
   OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
   USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include "expat.h"
#ifdef XML_UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <urlmon.h>
#include <wininet.h>
#include <stdio.h>
#include <tchar.h>
#include "xmlurl.h"
#include "xmlmime.h"

static int
processURL(XML_Parser parser, IMoniker *baseMoniker, const XML_Char *url);

typedef void (*StopHandler)(void *, HRESULT);

class Callback : public IBindStatusCallback {
public:
  // IUnknown methods
  STDMETHODIMP QueryInterface(REFIID,void **);
  STDMETHODIMP_(ULONG) AddRef();
  STDMETHODIMP_(ULONG) Release();
  // IBindStatusCallback methods
  STDMETHODIMP OnStartBinding(DWORD, IBinding *);
  STDMETHODIMP GetPriority(LONG *);
  STDMETHODIMP OnLowResource(DWORD);
  STDMETHODIMP OnProgress(ULONG, ULONG, ULONG, LPCWSTR);
  STDMETHODIMP OnStopBinding(HRESULT, LPCWSTR);
  STDMETHODIMP GetBindInfo(DWORD *, BINDINFO *);
  STDMETHODIMP OnDataAvailable(DWORD, DWORD, FORMATETC *, STGMEDIUM *);
  STDMETHODIMP OnObjectAvailable(REFIID, IUnknown *);
  Callback(XML_Parser, IMoniker *, StopHandler, void *);
  ~Callback();
  int externalEntityRef(const XML_Char *context,
                        const XML_Char *systemId, const XML_Char *publicId);
private:
  XML_Parser parser_;
  IMoniker *baseMoniker_;
  DWORD totalRead_;
  ULONG ref_;
  IBinding *pBinding_;
  StopHandler stopHandler_;
  void *stopArg_;
};

STDMETHODIMP_(ULONG)
Callback::AddRef()
{ 
  return ref_++;
}

STDMETHODIMP_(ULONG)
Callback::Release()
{ 
  if (--ref_ == 0) {
    delete this;
    return 0;
  }
  return ref_;
}

STDMETHODIMP
Callback::QueryInterface(REFIID riid, void** ppv)
{ 
  if (IsEqualGUID(riid, IID_IUnknown))
    *ppv = (IUnknown *)this;
  else if (IsEqualGUID(riid, IID_IBindStatusCallback))
    *ppv = (IBindStatusCallback *)this;
  else
    return E_NOINTERFACE;
  ((LPUNKNOWN)*ppv)->AddRef();
  return S_OK;
}

STDMETHODIMP
Callback::OnStartBinding(DWORD, IBinding* pBinding)
{
  pBinding_ = pBinding;
  pBinding->AddRef();
  return S_OK;
}

STDMETHODIMP
Callback::GetPriority(LONG *)
{
  return E_NOTIMPL;
}

STDMETHODIMP
Callback::OnLowResource(DWORD)
{
  return E_NOTIMPL;
}

STDMETHODIMP
Callback::OnProgress(ULONG, ULONG, ULONG, LPCWSTR)
{
  return S_OK;
}

STDMETHODIMP
Callback::OnStopBinding(HRESULT hr, LPCWSTR szError)
{
  if (pBinding_) {
    pBinding_->Release();
    pBinding_ = 0;
  }
  if (baseMoniker_) {
    baseMoniker_->Release();
    baseMoniker_ = 0;
  }
  stopHandler_(stopArg_, hr);
  return S_OK;
}

STDMETHODIMP
Callback::GetBindInfo(DWORD* pgrfBINDF, BINDINFO* pbindinfo)
{
  *pgrfBINDF = BINDF_ASYNCHRONOUS;
  return S_OK;
}

static void
reportError(XML_Parser parser)
{
  int code = XML_GetErrorCode(parser);
  const XML_Char *message = XML_ErrorString(code);
  if (message)
    _ftprintf(stderr, _T("%s:%d:%ld: %s\n"),
	     XML_GetBase(parser),
	     XML_GetErrorLineNumber(parser),
	     XML_GetErrorColumnNumber(parser),
	     message);
  else
    _ftprintf(stderr, _T("%s: (unknown message %d)\n"),
              XML_GetBase(parser), code);
}

STDMETHODIMP
Callback::OnDataAvailable(DWORD grfBSCF,
                          DWORD dwSize,
                          FORMATETC *pfmtetc,
                          STGMEDIUM* pstgmed)
{
  if (grfBSCF & BSCF_FIRSTDATANOTIFICATION) {
    IWinInetHttpInfo *hp;
    HRESULT hr = pBinding_->QueryInterface(IID_IWinInetHttpInfo,
                                           (void **)&hp);
    if (SUCCEEDED(hr)) {
      char contentType[1024];
      DWORD bufSize = sizeof(contentType);
      DWORD flags = 0;
      contentType[0] = 0;
      hr = hp->QueryInfo(HTTP_QUERY_CONTENT_TYPE, contentType,
                         &bufSize, 0, NULL);
      if (SUCCEEDED(hr)) {
	char charset[CHARSET_MAX];
	getXMLCharset(contentType, charset);
	if (charset[0]) {
#ifdef XML_UNICODE
	  XML_Char wcharset[CHARSET_MAX];
	  XML_Char *p1 = wcharset;
	  const char *p2 = charset;
	  while ((*p1++ = (unsigned char)*p2++) != 0)
	    ;
	  XML_SetEncoding(parser_, wcharset);
#else
	  XML_SetEncoding(parser_, charset);
#endif
	}
      }
      hp->Release();
    }
  }
  if (!parser_)
    return E_ABORT;
  if (pstgmed->tymed == TYMED_ISTREAM) {
    while (totalRead_ < dwSize) {
#define READ_MAX (64*1024)
      DWORD nToRead = dwSize - totalRead_;
      if (nToRead > READ_MAX)
	nToRead = READ_MAX;
      void *buf = XML_GetBuffer(parser_, nToRead);
      if (!buf) {
	_ftprintf(stderr, _T("out of memory\n"));
	return E_ABORT;
      }
      DWORD nRead;
      HRESULT hr = pstgmed->pstm->Read(buf, nToRead, &nRead);
      if (SUCCEEDED(hr)) {
	totalRead_ += nRead;
	if (!XML_ParseBuffer(parser_,
			     nRead,
			     (grfBSCF & BSCF_LASTDATANOTIFICATION) != 0
			     && totalRead_ == dwSize)) {
	  reportError(parser_);
	  return E_ABORT;
	}
      }
    }
  }
  return S_OK;
}

STDMETHODIMP
Callback::OnObjectAvailable(REFIID, IUnknown *)
{
  return S_OK;
}

int
Callback::externalEntityRef(const XML_Char *context,
                            const XML_Char *systemId,
                            const XML_Char *publicId)
{
  XML_Parser entParser = XML_ExternalEntityParserCreate(parser_, context, 0);
  XML_SetBase(entParser, systemId);
  int ret = processURL(entParser, baseMoniker_, systemId);
  XML_ParserFree(entParser);
  return ret;
}

Callback::Callback(XML_Parser parser, IMoniker *baseMoniker,
                   StopHandler stopHandler, void *stopArg)
: parser_(parser),
  baseMoniker_(baseMoniker),
  ref_(0),
  pBinding_(0),
  totalRead_(0),
  stopHandler_(stopHandler),
  stopArg_(stopArg)
{
  if (baseMoniker_)
    baseMoniker_->AddRef();
}

Callback::~Callback()
{
  if (pBinding_)
    pBinding_->Release();
  if (baseMoniker_)
    baseMoniker_->Release();
}

static int
externalEntityRef(void *arg,
                  const XML_Char *context,
                  const XML_Char *base,
                  const XML_Char *systemId,
                  const XML_Char *publicId)
{
  return ((Callback *)arg)->externalEntityRef(context, systemId, publicId);
}


static HRESULT
openStream(XML_Parser parser,
           IMoniker *baseMoniker,
           const XML_Char *uri,
           StopHandler stopHandler, void *stopArg)
{
  if (!XML_SetBase(parser, uri))
    return E_OUTOFMEMORY;
  HRESULT hr;
  IMoniker *m;
#ifdef XML_UNICODE
  hr = CreateURLMoniker(0, uri, &m);
#else
  LPWSTR uriw = new wchar_t[strlen(uri) + 1];
  for (int i = 0;; i++) {
    uriw[i] = uri[i];
    if (uriw[i] == 0)
      break;
  }
  hr = CreateURLMoniker(baseMoniker, uriw, &m);
  delete [] uriw;
#endif
  if (FAILED(hr))
    return hr;
  IBindStatusCallback *cb = new Callback(parser, m, stopHandler, stopArg);
  XML_SetExternalEntityRefHandler(parser, externalEntityRef);
  XML_SetExternalEntityRefHandlerArg(parser, cb);
  cb->AddRef();
  IBindCtx *b;
  if (FAILED(hr = CreateAsyncBindCtx(0, cb, 0, &b))) {
    cb->Release();
    m->Release();
    return hr;
  }
  cb->Release();
  IStream *pStream;
  hr = m->BindToStorage(b, 0, IID_IStream, (void **)&pStream);
  if (SUCCEEDED(hr)) {
    if (pStream)
      pStream->Release();
  }
  if (hr == MK_S_ASYNCHRONOUS)
    hr = S_OK;
  m->Release();
  b->Release();
  return hr;
}

struct QuitInfo {
  const XML_Char *url;
  HRESULT hr;
  int stop;
};

static void
winPerror(const XML_Char *url, HRESULT hr)
{
  LPVOID buf;
  if (FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER
		    | FORMAT_MESSAGE_FROM_HMODULE,
		    GetModuleHandleA("urlmon.dll"),
		    hr,
		    MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		    (LPTSTR) &buf,
		    0,
		    NULL)
      || FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER
		      | FORMAT_MESSAGE_FROM_SYSTEM,
		      0,
		      hr,
		      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		      (LPTSTR) &buf,
		      0,
		      NULL)) {
    /* The system error messages seem to end with a newline. */
    _ftprintf(stderr, _T("%s: %s"), url, buf);
    fflush(stderr);
    LocalFree(buf);
  }
  else
    _ftprintf(stderr, _T("%s: error %x\n"), url, hr);
}

static void
threadQuit(void *p, HRESULT hr)
{
  QuitInfo *qi = (QuitInfo *)p;
  qi->hr = hr;
  qi->stop = 1;
}

extern "C"
int
XML_URLInit(void)
{
  return SUCCEEDED(CoInitialize(0));
}

extern "C"
void
XML_URLUninit(void)
{
  CoUninitialize();
}

static int
processURL(XML_Parser parser, IMoniker *baseMoniker,
           const XML_Char *url)
{
  QuitInfo qi;
  qi.stop = 0;
  qi.url = url;

  XML_SetBase(parser, url);
  HRESULT hr = openStream(parser, baseMoniker, url, threadQuit, &qi);
  if (FAILED(hr)) {
    winPerror(url, hr);
    return 0;
  }
  else if (FAILED(qi.hr)) {
    winPerror(url, qi.hr);
    return 0;
  }
  MSG msg;
  while (!qi.stop && GetMessage (&msg, NULL, 0, 0)) {
    TranslateMessage (&msg);
    DispatchMessage (&msg);
  }
  return 1;
}

extern "C"
int
XML_ProcessURL(XML_Parser parser,
               const XML_Char *url,
               unsigned flags)
{
  return processURL(parser, 0, url);
}
