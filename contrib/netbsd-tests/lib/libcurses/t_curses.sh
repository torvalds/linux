
h_run()
{
	file="$(atf_get_srcdir)/tests/${1}"

	export COLUMNS=80
	export LINES=24
	$(atf_get_srcdir)/director \
	    -T $(atf_get_srcdir) \
	    -t atf \
	    -I $(atf_get_srcdir)/tests \
	    -C $(atf_get_srcdir)/check_files \
	    -s $(atf_get_srcdir)/slave $file || atf_fail "test ${file} failed"
}

atf_test_case startup
startup_head()
{
	atf_set "descr" "Checks curses initialisation sequence"
}
startup_body()
{
	h_run start
}

atf_test_case addch
addch_head()
{
	atf_set "descr" "Tests adding a chtype to stdscr"
}
addch_body()
{
	h_run addch
}

atf_test_case addchstr
addchstr_head()
{
	atf_set "descr" "Tests adding a chtype string to stdscr"
}
addchstr_body()
{
	h_run addchstr
}

atf_test_case addchnstr
addchnstr_head()
{
	atf_set "descr" "Tests adding bytes from a chtype string to stdscr"
}
addchnstr_body()
{
	h_run addchnstr
}

atf_test_case addstr
addstr_head()
{
	atf_set "descr" "Tests adding bytes from a string to stdscr"
}
addstr_body()
{
	h_run addstr
}

atf_test_case addnstr
addnstr_head()
{
	atf_set "descr" "Tests adding bytes from a string to stdscr"
}
addnstr_body()
{
	h_run addnstr
}

atf_test_case getch
getch_head()
{
	atf_set "descr" "Checks reading a character input"
}
getch_body()
{
	h_run getch
}

atf_test_case timeout
timeout_head()
{
	atf_set "descr" "Checks timeout when reading a character"
}
timeout_body()
{
	h_run timeout
}

atf_test_case window
window_head()
{
	atf_set "descr" "Checks window creation"
}
window_body()
{
	h_run window
}

atf_test_case wborder
wborder_head()
{
	atf_set "descr" "Checks drawing a border around a window"
}
wborder_body()
{
	h_run wborder
}

atf_test_case box
box_head()
{
	atf_set "descr" "Checks drawing a box around a window"
}
box_body()
{
	h_run box
}

atf_test_case wprintw
wprintw_head()
{
	atf_set "descr" "Checks printing to a window"
}
wprintw_body()
{
	h_run wprintw
}

atf_test_case wscrl
wscrl_head()
{
	atf_set "descr" "Check window scrolling"
}
wscrl_body()
{
	h_run wscrl
}

atf_test_case mvwin
mvwin_head()
{
	atf_set "descr" "Check moving a window"
}
mvwin_body()
{
	h_run mvwin
}

atf_test_case getstr
getstr_head()
{
	atf_set "descr" "Check getting a string from input"
}
getstr_body()
{
	h_run getstr
}

atf_test_case termattrs
termattrs_head()
{
	atf_set "descr" "Check the terminal attributes"
}
termattrs_body()
{
	h_run termattrs
}

atf_test_case assume_default_colors
assume_default_colors_head()
{
	atf_set "descr" "Check setting the default color pair"
}
assume_default_colors_body()
{
	h_run assume_default_colors
}

atf_test_case attributes
attributes_head()
{
	atf_set "descr" "Check setting, clearing and getting of attributes"
}
attributes_body()
{
	h_run attributes
}

atf_test_case beep
beep_head()
{
	atf_set "descr" "Check sending a beep"
}
beep_body()
{
	h_run beep
}

atf_test_case background
background_head()
{
	atf_set "descr" "Check setting background character and attributes for both stdscr and a window."
}
background_body()
{
	h_run background
}

atf_test_case can_change_color
can_change_color_head()
{
	atf_set "descr" "Check if the terminal can change colours"
}
can_change_color_body()
{
	h_run can_change_color
}

atf_test_case cbreak
cbreak_head()
{
	atf_set "descr" "Check cbreak mode works"
}
cbreak_body()
{
	h_run cbreak
}

atf_test_case clear
clear_head()
{
	atf_set "descr" "Check clear and erase work"
}
clear_body()
{
	h_run clear
}

atf_test_case copywin
copywin_head()
{
	atf_set "descr" "Check all the modes of copying a window work"
}
copywin_body()
{
	h_run copywin
}

atf_test_case curs_set
curs_set_head()
{
	atf_set "descr" "Check setting the cursor visibility works"
}
curs_set_body()
{
	h_run curs_set
}

atf_init_test_cases()
{
	atf_add_test_case startup
	atf_add_test_case addch
	atf_add_test_case addchstr
	atf_add_test_case addchnstr
	atf_add_test_case addstr
	atf_add_test_case addnstr
	atf_add_test_case getch
	atf_add_test_case timeout
	atf_add_test_case window
	atf_add_test_case wborder
	atf_add_test_case box
	atf_add_test_case wprintw
	atf_add_test_case wscrl
	atf_add_test_case mvwin
	atf_add_test_case getstr
	atf_add_test_case termattrs
	atf_add_test_case can_change_color
	atf_add_test_case assume_default_colors
	atf_add_test_case attributes
#	atf_add_test_case beep  # comment out for now - return is wrong
	atf_add_test_case background
	atf_add_test_case cbreak
	atf_add_test_case clear
	atf_add_test_case copywin
	atf_add_test_case curs_set
}

