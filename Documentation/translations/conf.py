# -*- coding: utf-8 -*-
# SPDX-License-Identifier: GPL-2.0

# -- Additinal options for LaTeX output ----------------------------------
# font config for ascii-art alignment

latex_elements['preamble']  += '''
    \\IfFontExistsTF{Noto Sans CJK SC}{
	% For CJK ascii-art alignment
	\\setmonofont{Noto Sans Mono CJK SC}[AutoFakeSlant]
    }{}
'''
