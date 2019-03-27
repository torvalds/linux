/*
 * ndis_events - Receive NdisMIndicateStatus() events using WMI
 * Copyright (c) 2004-2006, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#define _WIN32_WINNT    0x0400

#include "includes.h"

#ifndef COBJMACROS
#define COBJMACROS
#endif /* COBJMACROS */
#include <wbemidl.h>

#include "common.h"


static int wmi_refcnt = 0;
static int wmi_first = 1;

struct ndis_events_data {
	IWbemObjectSink sink;
	IWbemObjectSinkVtbl sink_vtbl;

	IWbemServices *pSvc;
	IWbemLocator *pLoc;

	HANDLE read_pipe, write_pipe, event_avail;
	UINT ref;
	int terminating;
	char *ifname; /* {GUID..} */
	WCHAR *adapter_desc;
};

#define BstrAlloc(x) (x) ? SysAllocString(x) : NULL
#define BstrFree(x) if (x) SysFreeString(x)

/* WBEM / WMI wrapper functions, to perform in-place conversion of WCHARs to
 * BSTRs */
HRESULT STDMETHODCALLTYPE call_IWbemServices_ExecQuery(
	IWbemServices *pSvc, LPCWSTR strQueryLanguage, LPCWSTR strQuery,
	long lFlags, IWbemContext *pCtx, IEnumWbemClassObject **ppEnum)
{
	BSTR bsQueryLanguage, bsQuery;
	HRESULT hr;

	bsQueryLanguage = BstrAlloc(strQueryLanguage);
	bsQuery = BstrAlloc(strQuery);

	hr = IWbemServices_ExecQuery(pSvc, bsQueryLanguage, bsQuery, lFlags,
				     pCtx, ppEnum);

	BstrFree(bsQueryLanguage);
	BstrFree(bsQuery);

	return hr;
}


HRESULT STDMETHODCALLTYPE call_IWbemServices_ExecNotificationQueryAsync(
	IWbemServices *pSvc, LPCWSTR strQueryLanguage, LPCWSTR strQuery,
	long lFlags, IWbemContext *pCtx, IWbemObjectSink *pResponseHandler)
{
	BSTR bsQueryLanguage, bsQuery;
	HRESULT hr;

	bsQueryLanguage = BstrAlloc(strQueryLanguage);
	bsQuery = BstrAlloc(strQuery);

	hr = IWbemServices_ExecNotificationQueryAsync(pSvc, bsQueryLanguage,
						      bsQuery, lFlags, pCtx,
						      pResponseHandler);

	BstrFree(bsQueryLanguage);
	BstrFree(bsQuery);

	return hr;
}


HRESULT STDMETHODCALLTYPE call_IWbemLocator_ConnectServer(
	IWbemLocator *pLoc, LPCWSTR strNetworkResource, LPCWSTR strUser,
	LPCWSTR strPassword, LPCWSTR strLocale, long lSecurityFlags,
	LPCWSTR strAuthority, IWbemContext *pCtx, IWbemServices **ppNamespace)
{
	BSTR bsNetworkResource, bsUser, bsPassword, bsLocale, bsAuthority;
	HRESULT hr;

	bsNetworkResource = BstrAlloc(strNetworkResource);
	bsUser = BstrAlloc(strUser);
	bsPassword = BstrAlloc(strPassword);
	bsLocale = BstrAlloc(strLocale);
	bsAuthority = BstrAlloc(strAuthority);

	hr = IWbemLocator_ConnectServer(pLoc, bsNetworkResource, bsUser,
					bsPassword, bsLocale, lSecurityFlags,
					bsAuthority, pCtx, ppNamespace);

	BstrFree(bsNetworkResource);
	BstrFree(bsUser);
	BstrFree(bsPassword);
	BstrFree(bsLocale);
	BstrFree(bsAuthority);

	return hr;
}


enum event_types { EVENT_CONNECT, EVENT_DISCONNECT, EVENT_MEDIA_SPECIFIC,
		   EVENT_ADAPTER_ARRIVAL, EVENT_ADAPTER_REMOVAL };

static int ndis_events_get_adapter(struct ndis_events_data *events,
				   const char *ifname, const char *desc);


static int ndis_events_constructor(struct ndis_events_data *events)
{
	events->ref = 1;

	if (!CreatePipe(&events->read_pipe, &events->write_pipe, NULL, 512)) {
		wpa_printf(MSG_ERROR, "CreatePipe() failed: %d",
			   (int) GetLastError());
		return -1;
	}
	events->event_avail = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (events->event_avail == NULL) {
		wpa_printf(MSG_ERROR, "CreateEvent() failed: %d",
			   (int) GetLastError());
		CloseHandle(events->read_pipe);
		CloseHandle(events->write_pipe);
		return -1;
	}

	return 0;
}


static void ndis_events_destructor(struct ndis_events_data *events)
{
	CloseHandle(events->read_pipe);
	CloseHandle(events->write_pipe);
	CloseHandle(events->event_avail);
	IWbemServices_Release(events->pSvc);
	IWbemLocator_Release(events->pLoc);
	if (--wmi_refcnt == 0)
		CoUninitialize();
}


static HRESULT STDMETHODCALLTYPE
ndis_events_query_interface(IWbemObjectSink *this, REFIID riid, void **obj)
{
	*obj = NULL;

	if (IsEqualIID(riid, &IID_IUnknown) ||
	    IsEqualIID(riid, &IID_IWbemObjectSink)) {
		*obj = this;
		IWbemObjectSink_AddRef(this);
		return NOERROR;
	}

	return E_NOINTERFACE;
}


static ULONG STDMETHODCALLTYPE ndis_events_add_ref(IWbemObjectSink *this)
{
	struct ndis_events_data *events = (struct ndis_events_data *) this;
	return ++events->ref;
}


static ULONG STDMETHODCALLTYPE ndis_events_release(IWbemObjectSink *this)
{
	struct ndis_events_data *events = (struct ndis_events_data *) this;

	if (--events->ref != 0)
		return events->ref;

	ndis_events_destructor(events);
	wpa_printf(MSG_DEBUG, "ndis_events: terminated");
	os_free(events->adapter_desc);
	os_free(events->ifname);
	os_free(events);
	return 0;
}


static int ndis_events_send_event(struct ndis_events_data *events,
				  enum event_types type,
				  char *data, size_t data_len)
{
	char buf[512], *pos, *end;
	int _type;
	DWORD written;

	end = buf + sizeof(buf);
	_type = (int) type;
	os_memcpy(buf, &_type, sizeof(_type));
	pos = buf + sizeof(_type);

	if (data) {
		if (2 + data_len > (size_t) (end - pos)) {
			wpa_printf(MSG_DEBUG, "Not enough room for send_event "
				   "data (%d)", data_len);
			return -1;
		}
		*pos++ = data_len >> 8;
		*pos++ = data_len & 0xff;
		os_memcpy(pos, data, data_len);
		pos += data_len;
	}

	if (WriteFile(events->write_pipe, buf, pos - buf, &written, NULL)) {
		SetEvent(events->event_avail);
		return 0;
	}
	wpa_printf(MSG_INFO, "WriteFile() failed: %d", (int) GetLastError());
	return -1;
}


static void ndis_events_media_connect(struct ndis_events_data *events)
{
	wpa_printf(MSG_DEBUG, "MSNdis_StatusMediaConnect");
	ndis_events_send_event(events, EVENT_CONNECT, NULL, 0);
}


static void ndis_events_media_disconnect(struct ndis_events_data *events)
{
	wpa_printf(MSG_DEBUG, "MSNdis_StatusMediaDisconnect");
	ndis_events_send_event(events, EVENT_DISCONNECT, NULL, 0);
}


static void ndis_events_media_specific(struct ndis_events_data *events,
				       IWbemClassObject *pObj)
{
	VARIANT vt;
	HRESULT hr;
	LONG lower, upper, k;
	UCHAR ch;
	char *data, *pos;
	size_t data_len;

	wpa_printf(MSG_DEBUG, "MSNdis_StatusMediaSpecificIndication");

	/* This is the StatusBuffer from NdisMIndicateStatus() call */
	hr = IWbemClassObject_Get(pObj, L"NdisStatusMediaSpecificIndication",
				  0, &vt, NULL, NULL);
	if (FAILED(hr)) {
		wpa_printf(MSG_DEBUG, "Could not get "
			   "NdisStatusMediaSpecificIndication from "
			   "the object?!");
		return;
	}

	SafeArrayGetLBound(V_ARRAY(&vt), 1, &lower);
	SafeArrayGetUBound(V_ARRAY(&vt), 1, &upper);
	data_len = upper - lower + 1;
	data = os_malloc(data_len);
	if (data == NULL) {
		wpa_printf(MSG_DEBUG, "Failed to allocate buffer for event "
			   "data");
		VariantClear(&vt);
		return;
	}

	pos = data;
	for (k = lower; k <= upper; k++) {
		SafeArrayGetElement(V_ARRAY(&vt), &k, &ch);
		*pos++ = ch;
	}
	wpa_hexdump(MSG_DEBUG, "MediaSpecificEvent", (u8 *) data, data_len);

	VariantClear(&vt);

	ndis_events_send_event(events, EVENT_MEDIA_SPECIFIC, data, data_len);

	os_free(data);
}


static void ndis_events_adapter_arrival(struct ndis_events_data *events)
{
	wpa_printf(MSG_DEBUG, "MSNdis_NotifyAdapterArrival");
	ndis_events_send_event(events, EVENT_ADAPTER_ARRIVAL, NULL, 0);
}


static void ndis_events_adapter_removal(struct ndis_events_data *events)
{
	wpa_printf(MSG_DEBUG, "MSNdis_NotifyAdapterRemoval");
	ndis_events_send_event(events, EVENT_ADAPTER_REMOVAL, NULL, 0);
}


static HRESULT STDMETHODCALLTYPE
ndis_events_indicate(IWbemObjectSink *this, long lObjectCount,
		     IWbemClassObject __RPC_FAR *__RPC_FAR *ppObjArray)
{
	struct ndis_events_data *events = (struct ndis_events_data *) this;
	long i;

	if (events->terminating) {
		wpa_printf(MSG_DEBUG, "ndis_events_indicate: Ignore "
			   "indication - terminating");
		return WBEM_NO_ERROR;
	}
	/* wpa_printf(MSG_DEBUG, "Notification received - %d object(s)",
	   lObjectCount); */

	for (i = 0; i < lObjectCount; i++) {
		IWbemClassObject *pObj = ppObjArray[i];
		HRESULT hr;
		VARIANT vtClass, vt;

		hr = IWbemClassObject_Get(pObj, L"__CLASS", 0, &vtClass, NULL,
					  NULL);
		if (FAILED(hr)) {
			wpa_printf(MSG_DEBUG, "Failed to get __CLASS from "
				   "event.");
			break;
		}
		/* wpa_printf(MSG_DEBUG, "CLASS: '%S'", vtClass.bstrVal); */

		hr = IWbemClassObject_Get(pObj, L"InstanceName", 0, &vt, NULL,
					  NULL);
		if (FAILED(hr)) {
			wpa_printf(MSG_DEBUG, "Failed to get InstanceName "
				   "from event.");
			VariantClear(&vtClass);
			break;
		}

		if (wcscmp(vtClass.bstrVal,
			   L"MSNdis_NotifyAdapterArrival") == 0) {
			wpa_printf(MSG_DEBUG, "ndis_events_indicate: Try to "
				   "update adapter description since it may "
				   "have changed with new adapter instance");
			ndis_events_get_adapter(events, events->ifname, NULL);
		}

		if (wcscmp(events->adapter_desc, vt.bstrVal) != 0) {
			wpa_printf(MSG_DEBUG, "ndis_events_indicate: Ignore "
				   "indication for foreign adapter: "
				   "InstanceName: '%S' __CLASS: '%S'",
				   vt.bstrVal, vtClass.bstrVal);
			VariantClear(&vtClass);
			VariantClear(&vt);
			continue;
		}
		VariantClear(&vt);

		if (wcscmp(vtClass.bstrVal,
			   L"MSNdis_StatusMediaSpecificIndication") == 0) {
			ndis_events_media_specific(events, pObj);
		} else if (wcscmp(vtClass.bstrVal,
				  L"MSNdis_StatusMediaConnect") == 0) {
			ndis_events_media_connect(events);
		} else if (wcscmp(vtClass.bstrVal,
				  L"MSNdis_StatusMediaDisconnect") == 0) {
			ndis_events_media_disconnect(events);
		} else if (wcscmp(vtClass.bstrVal,
				  L"MSNdis_NotifyAdapterArrival") == 0) {
			ndis_events_adapter_arrival(events);
		} else if (wcscmp(vtClass.bstrVal,
				  L"MSNdis_NotifyAdapterRemoval") == 0) {
			ndis_events_adapter_removal(events);
		} else {
			wpa_printf(MSG_DEBUG, "Unepected event - __CLASS: "
				   "'%S'", vtClass.bstrVal);
		}

		VariantClear(&vtClass);
	}

	return WBEM_NO_ERROR;
}


static HRESULT STDMETHODCALLTYPE
ndis_events_set_status(IWbemObjectSink *this, long lFlags, HRESULT hResult,
		       BSTR strParam, IWbemClassObject __RPC_FAR *pObjParam)
{
	return WBEM_NO_ERROR;
}


static int notification_query(IWbemObjectSink *pDestSink,
			      IWbemServices *pSvc, const char *class_name)
{
	HRESULT hr;
	WCHAR query[256];

	_snwprintf(query, 256,
		  L"SELECT * FROM %S", class_name);
	wpa_printf(MSG_DEBUG, "ndis_events: WMI: %S", query);
	hr = call_IWbemServices_ExecNotificationQueryAsync(
		pSvc, L"WQL", query, 0, 0, pDestSink);
	if (FAILED(hr)) {
		wpa_printf(MSG_DEBUG, "ExecNotificationQueryAsync for %s "
			   "failed with hresult of 0x%x",
			   class_name, (int) hr);
		return -1;
	}

	return 0;
}


static int register_async_notification(IWbemObjectSink *pDestSink,
				       IWbemServices *pSvc)
{
	int i;
	const char *class_list[] = {
		"MSNdis_StatusMediaConnect",
		"MSNdis_StatusMediaDisconnect",
		"MSNdis_StatusMediaSpecificIndication",
		"MSNdis_NotifyAdapterArrival",
		"MSNdis_NotifyAdapterRemoval",
		NULL
	};

	for (i = 0; class_list[i]; i++) {
		if (notification_query(pDestSink, pSvc, class_list[i]) < 0)
			return -1;
	}

	return 0;
}


void ndis_events_deinit(struct ndis_events_data *events)
{
	events->terminating = 1;
	IWbemServices_CancelAsyncCall(events->pSvc, &events->sink);
	IWbemObjectSink_Release(&events->sink);
	/*
	 * Rest of deinitialization is done in ndis_events_destructor() once
	 * all reference count drops to zero.
	 */
}


static int ndis_events_use_desc(struct ndis_events_data *events,
				const char *desc)
{
	char *tmp, *pos;
	size_t len;

	if (desc == NULL) {
		if (events->adapter_desc == NULL)
			return -1;
		/* Continue using old description */
		return 0;
	}

	tmp = os_strdup(desc);
	if (tmp == NULL)
		return -1;

	pos = os_strstr(tmp, " (Microsoft's Packet Scheduler)");
	if (pos)
		*pos = '\0';

	len = os_strlen(tmp);
	events->adapter_desc = os_malloc((len + 1) * sizeof(WCHAR));
	if (events->adapter_desc == NULL) {
		os_free(tmp);
		return -1;
	}
	_snwprintf(events->adapter_desc, len + 1, L"%S", tmp);
	os_free(tmp);
	return 0;
}


static int ndis_events_get_adapter(struct ndis_events_data *events,
				   const char *ifname, const char *desc)
{
	HRESULT hr;
	IWbemServices *pSvc;
#define MAX_QUERY_LEN 256
	WCHAR query[MAX_QUERY_LEN];
	IEnumWbemClassObject *pEnumerator;
	IWbemClassObject *pObj;
	ULONG uReturned;
	VARIANT vt;
	int len, pos;

	/*
	 * Try to get adapter descriptor through WMI CIMv2 Win32_NetworkAdapter
	 * to have better probability of matching with InstanceName from
	 * MSNdis events. If this fails, use the provided description.
	 */

	os_free(events->adapter_desc);
	events->adapter_desc = NULL;

	hr = call_IWbemLocator_ConnectServer(
		events->pLoc, L"ROOT\\CIMV2", NULL, NULL, 0, 0, 0, 0, &pSvc);
	if (FAILED(hr)) {
		wpa_printf(MSG_ERROR, "ndis_events: Could not connect to WMI "
			   "server (ROOT\\CIMV2) - error 0x%x", (int) hr);
		return ndis_events_use_desc(events, desc);
	}
	wpa_printf(MSG_DEBUG, "ndis_events: Connected to ROOT\\CIMV2.");

	_snwprintf(query, MAX_QUERY_LEN,
		  L"SELECT Index FROM Win32_NetworkAdapterConfiguration "
		  L"WHERE SettingID='%S'", ifname);
	wpa_printf(MSG_DEBUG, "ndis_events: WMI: %S", query);

	hr = call_IWbemServices_ExecQuery(
		pSvc, L"WQL", query,
		WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
		NULL, &pEnumerator);
	if (!SUCCEEDED(hr)) {
		wpa_printf(MSG_DEBUG, "ndis_events: Failed to query interface "
			   "GUID from Win32_NetworkAdapterConfiguration: "
			   "0x%x", (int) hr);
		IWbemServices_Release(pSvc);
		return ndis_events_use_desc(events, desc);
	}

	uReturned = 0;
	hr = IEnumWbemClassObject_Next(pEnumerator, WBEM_INFINITE, 1,
				       &pObj, &uReturned);
	if (!SUCCEEDED(hr) || uReturned == 0) {
		wpa_printf(MSG_DEBUG, "ndis_events: Failed to find interface "
			   "GUID from Win32_NetworkAdapterConfiguration: "
			   "0x%x", (int) hr);
		IEnumWbemClassObject_Release(pEnumerator);
		IWbemServices_Release(pSvc);
		return ndis_events_use_desc(events, desc);
	}
	IEnumWbemClassObject_Release(pEnumerator);

	VariantInit(&vt);
	hr = IWbemClassObject_Get(pObj, L"Index", 0, &vt, NULL, NULL);
	if (!SUCCEEDED(hr)) {
		wpa_printf(MSG_DEBUG, "ndis_events: Failed to get Index from "
			   "Win32_NetworkAdapterConfiguration: 0x%x",
			   (int) hr);
		IWbemServices_Release(pSvc);
		return ndis_events_use_desc(events, desc);
	}

	_snwprintf(query, MAX_QUERY_LEN,
		  L"SELECT Name,PNPDeviceID FROM Win32_NetworkAdapter WHERE "
		  L"Index=%d",
		  vt.uintVal);
	wpa_printf(MSG_DEBUG, "ndis_events: WMI: %S", query);
	VariantClear(&vt);
	IWbemClassObject_Release(pObj);

	hr = call_IWbemServices_ExecQuery(
		pSvc, L"WQL", query,
		WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
		NULL, &pEnumerator);
	if (!SUCCEEDED(hr)) {
		wpa_printf(MSG_DEBUG, "ndis_events: Failed to query interface "
			   "from Win32_NetworkAdapter: 0x%x", (int) hr);
		IWbemServices_Release(pSvc);
		return ndis_events_use_desc(events, desc);
	}

	uReturned = 0;
	hr = IEnumWbemClassObject_Next(pEnumerator, WBEM_INFINITE, 1,
				       &pObj, &uReturned);
	if (!SUCCEEDED(hr) || uReturned == 0) {
		wpa_printf(MSG_DEBUG, "ndis_events: Failed to find interface "
			   "from Win32_NetworkAdapter: 0x%x", (int) hr);
		IEnumWbemClassObject_Release(pEnumerator);
		IWbemServices_Release(pSvc);
		return ndis_events_use_desc(events, desc);
	}
	IEnumWbemClassObject_Release(pEnumerator);

	hr = IWbemClassObject_Get(pObj, L"Name", 0, &vt, NULL, NULL);
	if (!SUCCEEDED(hr)) {
		wpa_printf(MSG_DEBUG, "ndis_events: Failed to get Name from "
			   "Win32_NetworkAdapter: 0x%x", (int) hr);
		IWbemClassObject_Release(pObj);
		IWbemServices_Release(pSvc);
		return ndis_events_use_desc(events, desc);
	}

	wpa_printf(MSG_DEBUG, "ndis_events: Win32_NetworkAdapter::Name='%S'",
		   vt.bstrVal);
	events->adapter_desc = _wcsdup(vt.bstrVal);
	VariantClear(&vt);

	/*
	 * Try to get even better candidate for matching with InstanceName
	 * from Win32_PnPEntity. This is needed at least for some USB cards
	 * that can change the InstanceName whenever being unplugged and
	 * plugged again.
	 */

	hr = IWbemClassObject_Get(pObj, L"PNPDeviceID", 0, &vt, NULL, NULL);
	if (!SUCCEEDED(hr)) {
		wpa_printf(MSG_DEBUG, "ndis_events: Failed to get PNPDeviceID "
			   "from Win32_NetworkAdapter: 0x%x", (int) hr);
		IWbemClassObject_Release(pObj);
		IWbemServices_Release(pSvc);
		if (events->adapter_desc == NULL)
			return ndis_events_use_desc(events, desc);
		return 0; /* use Win32_NetworkAdapter::Name */
	}

	wpa_printf(MSG_DEBUG, "ndis_events: Win32_NetworkAdapter::PNPDeviceID="
		   "'%S'", vt.bstrVal);

	len = _snwprintf(query, MAX_QUERY_LEN,
			L"SELECT Name FROM Win32_PnPEntity WHERE DeviceID='");
	if (len < 0 || len >= MAX_QUERY_LEN - 1) {
		VariantClear(&vt);
		IWbemClassObject_Release(pObj);
		IWbemServices_Release(pSvc);
		if (events->adapter_desc == NULL)
			return ndis_events_use_desc(events, desc);
		return 0; /* use Win32_NetworkAdapter::Name */
	}

	/* Escape \ as \\ */
	for (pos = 0; vt.bstrVal[pos] && len < MAX_QUERY_LEN - 2; pos++) {
		if (vt.bstrVal[pos] == '\\') {
			if (len >= MAX_QUERY_LEN - 3)
				break;
			query[len++] = '\\';
		}
		query[len++] = vt.bstrVal[pos];
	}
	query[len++] = L'\'';
	query[len] = L'\0';
	VariantClear(&vt);
	IWbemClassObject_Release(pObj);
	wpa_printf(MSG_DEBUG, "ndis_events: WMI: %S", query);

	hr = call_IWbemServices_ExecQuery(
		pSvc, L"WQL", query,
		WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
		NULL, &pEnumerator);
	if (!SUCCEEDED(hr)) {
		wpa_printf(MSG_DEBUG, "ndis_events: Failed to query interface "
			   "Name from Win32_PnPEntity: 0x%x", (int) hr);
		IWbemServices_Release(pSvc);
		if (events->adapter_desc == NULL)
			return ndis_events_use_desc(events, desc);
		return 0; /* use Win32_NetworkAdapter::Name */
	}

	uReturned = 0;
	hr = IEnumWbemClassObject_Next(pEnumerator, WBEM_INFINITE, 1,
				       &pObj, &uReturned);
	if (!SUCCEEDED(hr) || uReturned == 0) {
		wpa_printf(MSG_DEBUG, "ndis_events: Failed to find interface "
			   "from Win32_PnPEntity: 0x%x", (int) hr);
		IEnumWbemClassObject_Release(pEnumerator);
		IWbemServices_Release(pSvc);
		if (events->adapter_desc == NULL)
			return ndis_events_use_desc(events, desc);
		return 0; /* use Win32_NetworkAdapter::Name */
	}
	IEnumWbemClassObject_Release(pEnumerator);

	hr = IWbemClassObject_Get(pObj, L"Name", 0, &vt, NULL, NULL);
	if (!SUCCEEDED(hr)) {
		wpa_printf(MSG_DEBUG, "ndis_events: Failed to get Name from "
			   "Win32_PnPEntity: 0x%x", (int) hr);
		IWbemClassObject_Release(pObj);
		IWbemServices_Release(pSvc);
		if (events->adapter_desc == NULL)
			return ndis_events_use_desc(events, desc);
		return 0; /* use Win32_NetworkAdapter::Name */
	}

	wpa_printf(MSG_DEBUG, "ndis_events: Win32_PnPEntity::Name='%S'",
		   vt.bstrVal);
	os_free(events->adapter_desc);
	events->adapter_desc = _wcsdup(vt.bstrVal);
	VariantClear(&vt);

	IWbemClassObject_Release(pObj);

	IWbemServices_Release(pSvc);

	if (events->adapter_desc == NULL)
		return ndis_events_use_desc(events, desc);

	return 0;
}


struct ndis_events_data *
ndis_events_init(HANDLE *read_pipe, HANDLE *event_avail,
		 const char *ifname, const char *desc)
{
	HRESULT hr;
	IWbemObjectSink *pSink;
	struct ndis_events_data *events;

	events = os_zalloc(sizeof(*events));
	if (events == NULL) {
		wpa_printf(MSG_ERROR, "Could not allocate sink for events.");
		return NULL;
	}
	events->ifname = os_strdup(ifname);
	if (events->ifname == NULL) {
		os_free(events);
		return NULL;
	}

	if (wmi_refcnt++ == 0) {
		hr = CoInitializeEx(0, COINIT_MULTITHREADED);
		if (FAILED(hr)) {
			wpa_printf(MSG_ERROR, "CoInitializeEx() failed - "
				   "returned 0x%x", (int) hr);
			os_free(events);
			return NULL;
		}
	}

	if (wmi_first) {
		/* CoInitializeSecurity() must be called once and only once
		 * per process, so let's use wmi_first flag to protect against
		 * multiple calls. */
		wmi_first = 0;

		hr = CoInitializeSecurity(NULL, -1, NULL, NULL,
					  RPC_C_AUTHN_LEVEL_PKT_PRIVACY,
					  RPC_C_IMP_LEVEL_IMPERSONATE,
					  NULL, EOAC_SECURE_REFS, NULL);
		if (FAILED(hr)) {
			wpa_printf(MSG_ERROR, "CoInitializeSecurity() failed "
				   "- returned 0x%x", (int) hr);
			os_free(events);
			return NULL;
		}
	}

	hr = CoCreateInstance(&CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER,
			      &IID_IWbemLocator,
			      (LPVOID *) (void *) &events->pLoc);
	if (FAILED(hr)) {
		wpa_printf(MSG_ERROR, "CoCreateInstance() failed - returned "
			   "0x%x", (int) hr);
		CoUninitialize();
		os_free(events);
		return NULL;
	}

	if (ndis_events_get_adapter(events, ifname, desc) < 0) {
		CoUninitialize();
		os_free(events);
		return NULL;
	}
	wpa_printf(MSG_DEBUG, "ndis_events: use adapter descriptor '%S'",
		   events->adapter_desc);

	hr = call_IWbemLocator_ConnectServer(
		events->pLoc, L"ROOT\\WMI", NULL, NULL,
		0, 0, 0, 0, &events->pSvc);
	if (FAILED(hr)) {
		wpa_printf(MSG_ERROR, "Could not connect to server - error "
			   "0x%x", (int) hr);
		CoUninitialize();
		os_free(events->adapter_desc);
		os_free(events);
		return NULL;
	}
	wpa_printf(MSG_DEBUG, "Connected to ROOT\\WMI.");

	ndis_events_constructor(events);
	pSink = &events->sink;
	pSink->lpVtbl = &events->sink_vtbl;
	events->sink_vtbl.QueryInterface = ndis_events_query_interface;
	events->sink_vtbl.AddRef = ndis_events_add_ref;
	events->sink_vtbl.Release = ndis_events_release;
	events->sink_vtbl.Indicate = ndis_events_indicate;
	events->sink_vtbl.SetStatus = ndis_events_set_status;

	if (register_async_notification(pSink, events->pSvc) < 0) {
		wpa_printf(MSG_DEBUG, "Failed to register async "
			   "notifications");
		ndis_events_destructor(events);
		os_free(events->adapter_desc);
		os_free(events);
		return NULL;
	}

	*read_pipe = events->read_pipe;
	*event_avail = events->event_avail;

	return events;
}
