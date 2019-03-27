;;; This Emacs Lisp file defines a C indentation style for OpenSSL.
;;;
;;; This definition is for the "CC mode" package, which is the default
;;; mode for editing C source files in Emacs 20, not for the older
;;; c-mode.el (which was the default in less recent releases of Emacs 19).
;;;
;;; Recommended use is to add this line in your .emacs:
;;;
;;;   (load (expand-file-name "~/PATH/TO/openssl-c-indent.el"))
;;;
;;; To activate this indentation style, visit a C file, type
;;; M-x c-set-style <RET> (or C-c . for short), and enter "eay".
;;; To toggle the auto-newline feature of CC mode, type C-c C-a.
;;;
;;; If you're an OpenSSL developer, you might find it more comfortable
;;; to have this style be permanent in your OpenSSL development
;;; directory.  To have that, please perform this:
;;;
;;;    M-x add-dir-local-variable <RET> c-mode <RET> c-file-style <RET>
;;;    "OpenSSL-II" <RET>
;;;
;;; A new buffer with .dir-locals.el will appear.  Save it (C-x C-s).
;;;
;;; Alternatively, have a look at dir-locals.example.el

;;; For suggesting improvements, please send e-mail to levitte@openssl.org.

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Note, it could be easy to inherit from the "gnu" style...  however,
;; one never knows if that style will change somewhere in the future,
;; so I've chosen to copy the "gnu" style values explicitly instead
;; and mark them with a comment.                // RLevitte 2015-08-31
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(c-add-style "OpenSSL-II"
             '((c-basic-offset . 4)
               (indent-tabs-mode . nil)
               (fill-column . 78)
               (comment-column . 33)
               (c-comment-only-line-offset 0 . 0)            ; From "gnu" style
               (c-hanging-braces-alist                       ; From "gnu" style
                (substatement-open before after)             ; From "gnu" style
                (arglist-cont-nonempty))                     ; From "gnu" style
               (c-offsets-alist
                (statement-block-intro . +)                  ; From "gnu" style
                (knr-argdecl-intro . 0)
                (knr-argdecl . 0)
                (substatement-open . +)                      ; From "gnu" style
                (substatement-label . 0)                     ; From "gnu" style
                (label . 1)
                (statement-case-open . +)                    ; From "gnu" style
                (statement-cont . +)                         ; From "gnu" style
                (arglist-intro . c-lineup-arglist-intro-after-paren) ; From "gnu" style
                (arglist-close . c-lineup-arglist)           ; From "gnu" style
                (inline-open . 0)                            ; From "gnu" style
                (brace-list-open . +)                        ; From "gnu" style
                (inextern-lang . 0)     ; Don't indent inside extern block
                (topmost-intro-cont first c-lineup-topmost-intro-cont
                                    c-lineup-gnu-DEFUN-intro-cont) ; From "gnu" style
                )
               (c-special-indent-hook . c-gnu-impose-minimum) ; From "gnu" style
               (c-block-comment-prefix . "* ")
               ))
