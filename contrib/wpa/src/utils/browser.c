/*
 * Hotspot 2.0 client - Web browser using WebKit
 * Copyright (c) 2013, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"
#include <webkit/webkit.h>

#include "common.h"
#include "browser.h"


struct browser_context {
	GtkWidget *win;
	int success;
	int progress;
	char *hover_link;
	char *title;
};

static void win_cb_destroy(GtkWidget *win, struct browser_context *ctx)
{
	wpa_printf(MSG_DEBUG, "BROWSER:%s", __func__);
	gtk_main_quit();
}


static void browser_update_title(struct browser_context *ctx)
{
	char buf[100];

	if (ctx->hover_link) {
		gtk_window_set_title(GTK_WINDOW(ctx->win), ctx->hover_link);
		return;
	}

	if (ctx->progress == 100) {
		gtk_window_set_title(GTK_WINDOW(ctx->win),
				     ctx->title ? ctx->title :
				     "Hotspot 2.0 client");
		return;
	}

	snprintf(buf, sizeof(buf), "[%d%%] %s", ctx->progress,
		 ctx->title ? ctx->title : "Hotspot 2.0 client");
	gtk_window_set_title(GTK_WINDOW(ctx->win), buf);
}


static void view_cb_notify_progress(WebKitWebView *view, GParamSpec *pspec,
				    struct browser_context *ctx)
{
	ctx->progress = 100 * webkit_web_view_get_progress(view);
	wpa_printf(MSG_DEBUG, "BROWSER:%s progress=%d", __func__,
		   ctx->progress);
	browser_update_title(ctx);
}


static void view_cb_notify_load_status(WebKitWebView *view, GParamSpec *pspec,
				       struct browser_context *ctx)
{
	int status = webkit_web_view_get_load_status(view);
	wpa_printf(MSG_DEBUG, "BROWSER:%s load-status=%d uri=%s",
		   __func__, status, webkit_web_view_get_uri(view));
}


static void view_cb_resource_request_starting(WebKitWebView *view,
					      WebKitWebFrame *frame,
					      WebKitWebResource *res,
					      WebKitNetworkRequest *req,
					      WebKitNetworkResponse *resp,
					      struct browser_context *ctx)
{
	const gchar *uri = webkit_network_request_get_uri(req);
	wpa_printf(MSG_DEBUG, "BROWSER:%s uri=%s", __func__, uri);
	if (g_str_has_suffix(uri, "/favicon.ico"))
		webkit_network_request_set_uri(req, "about:blank");
	if (g_str_has_prefix(uri, "osu://")) {
		ctx->success = atoi(uri + 6);
		gtk_main_quit();
	}
	if (g_str_has_prefix(uri, "http://localhost:12345")) {
		/*
		 * This is used as a special trigger to indicate that the
		 * user exchange has been completed.
		 */
		ctx->success = 1;
		gtk_main_quit();
	}
}


static gboolean view_cb_mime_type_policy_decision(
	WebKitWebView *view, WebKitWebFrame *frame, WebKitNetworkRequest *req,
	gchar *mime, WebKitWebPolicyDecision *policy,
	struct browser_context *ctx)
{
	wpa_printf(MSG_DEBUG, "BROWSER:%s mime=%s", __func__, mime);

	if (!webkit_web_view_can_show_mime_type(view, mime)) {
		webkit_web_policy_decision_download(policy);
		return TRUE;
	}

	return FALSE;
}


static gboolean view_cb_download_requested(WebKitWebView *view,
					   WebKitDownload *dl,
					   struct browser_context *ctx)
{
	const gchar *uri;
	uri = webkit_download_get_uri(dl);
	wpa_printf(MSG_DEBUG, "BROWSER:%s uri=%s", __func__, uri);
	return FALSE;
}


static void view_cb_hovering_over_link(WebKitWebView *view, gchar *title,
				       gchar *uri, struct browser_context *ctx)
{
	wpa_printf(MSG_DEBUG, "BROWSER:%s title=%s uri=%s", __func__, title,
		   uri);
	os_free(ctx->hover_link);
	if (uri)
		ctx->hover_link = os_strdup(uri);
	else
		ctx->hover_link = NULL;

	browser_update_title(ctx);
}


static void view_cb_title_changed(WebKitWebView *view, WebKitWebFrame *frame,
				  const char *title,
				  struct browser_context *ctx)
{
	wpa_printf(MSG_DEBUG, "BROWSER:%s title=%s", __func__, title);
	os_free(ctx->title);
	ctx->title = os_strdup(title);
	browser_update_title(ctx);
}


int hs20_web_browser(const char *url)
{
	GtkWidget *scroll;
	SoupSession *s;
	WebKitWebView *view;
	WebKitWebSettings *settings;
	struct browser_context ctx;

	memset(&ctx, 0, sizeof(ctx));
	if (!gtk_init_check(NULL, NULL))
		return -1;

	s = webkit_get_default_session();
	g_object_set(G_OBJECT(s), "ssl-ca-file",
		     "/etc/ssl/certs/ca-certificates.crt", NULL);
	g_object_set(G_OBJECT(s), "ssl-strict", FALSE, NULL);

	ctx.win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_wmclass(GTK_WINDOW(ctx.win), "Hotspot 2.0 client",
			       "Hotspot 2.0 client");
	gtk_window_set_default_size(GTK_WINDOW(ctx.win), 800, 600);

	scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
				       GTK_POLICY_NEVER, GTK_POLICY_NEVER);

	g_signal_connect(G_OBJECT(ctx.win), "destroy",
			 G_CALLBACK(win_cb_destroy), &ctx);

	view = WEBKIT_WEB_VIEW(webkit_web_view_new());
	g_signal_connect(G_OBJECT(view), "notify::progress",
			 G_CALLBACK(view_cb_notify_progress), &ctx);
	g_signal_connect(G_OBJECT(view), "notify::load-status",
			 G_CALLBACK(view_cb_notify_load_status), &ctx);
	g_signal_connect(G_OBJECT(view), "resource-request-starting",
			 G_CALLBACK(view_cb_resource_request_starting), &ctx);
	g_signal_connect(G_OBJECT(view), "mime-type-policy-decision-requested",
			 G_CALLBACK(view_cb_mime_type_policy_decision), &ctx);
	g_signal_connect(G_OBJECT(view), "download-requested",
			 G_CALLBACK(view_cb_download_requested), &ctx);
	g_signal_connect(G_OBJECT(view), "hovering-over-link",
			 G_CALLBACK(view_cb_hovering_over_link), &ctx);
	g_signal_connect(G_OBJECT(view), "title-changed",
			 G_CALLBACK(view_cb_title_changed), &ctx);

	gtk_container_add(GTK_CONTAINER(scroll), GTK_WIDGET(view));
	gtk_container_add(GTK_CONTAINER(ctx.win), GTK_WIDGET(scroll));

	gtk_widget_grab_focus(GTK_WIDGET(view));
	gtk_widget_show_all(ctx.win);

	settings = webkit_web_view_get_settings(view);
	g_object_set(G_OBJECT(settings), "user-agent",
		     "Mozilla/5.0 (X11; U; Unix; en-US) "
		     "AppleWebKit/537.15 (KHTML, like Gecko) "
		     "hs20-client/1.0", NULL);
	g_object_set(G_OBJECT(settings), "auto-load-images", TRUE, NULL);

	webkit_web_view_load_uri(view, url);

	gtk_main();
	gtk_widget_destroy(ctx.win);
	while (gtk_events_pending())
		gtk_main_iteration();

	free(ctx.hover_link);
	free(ctx.title);
	return ctx.success;
}
