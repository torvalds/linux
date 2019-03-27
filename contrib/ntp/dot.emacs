;; This is how Dave Mills likes to see the NTP code formatted.

(defconst ntp-c-style
  '((c-basic-offset  . 8)
    (fill-column     . 72)
    (c-offsets-alist . ((arglist-intro	      . +)
			(case-label	      . *)
			(statement-case-intro . *)
			(statement-cont	      . *)
			(substatement-open    . 0))))
  "David L. Mills; NTP code indentation style")

(defun ntp-c-mode-common-hook ()
  ;; add ntp c style
  (c-add-style "ntp" ntp-c-style nil))

(add-hook 'c-mode-common-hook 'ntp-c-mode-common-hook)

;; 1997112600
