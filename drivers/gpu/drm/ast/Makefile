#
# Makefile for the drm device driver.  This driver provides support for the
# Direct Rendering Infrastructure (DRI) in XFree86 4.1.0 and higher.

ccflags-y := -Iinclude/drm

ast-y := ast_drv.o ast_main.o ast_mode.o ast_fb.o ast_ttm.o ast_post.o ast_dp501.o

obj-$(CONFIG_DRM_AST) := ast.o
