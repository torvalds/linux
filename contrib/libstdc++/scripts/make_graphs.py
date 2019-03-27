#!/usr/bin/python

import sys
import commands
import re
from xml.dom import minidom
from BeautifulSoup import BeautifulSoup
import make_graph

class exception:
	pass
 
res_div_re = re.compile('(.*?)_res_div')
settings_div_re = re.compile('(.*?)_settings_div')	


gray_border_div_str = '<div style = "border-style: dotted; border-width: 1px; border-color: lightgray">'
space_div_str = '<div style = "width: 100%; height: 20px">'



def logical_build_from_build(build):
	if build == 'gcc':
		return 'g++'
	if build == 'msvc':
		return 'msvc++'
	if build == 'local':
		return 'local'
	sys.stderr.write(build)
	raise exception
 
 
def img_title_from_origs(label, title, base_build_ref, build_name, logical_build_name):
	title = title.replace('_tt_', '<tt>')
	title = title.replace('_455tt_', '</tt>')
	title = title.replace('_b_', '<b>')
	title = title.replace('_455b_', '</b>')
	title = title.replace('_456', ',')
	title = title.replace('_457', '[]')
	title = title.replace('_', ' ')
	return '%s: %s - <a href = "%s_performance_tests.html#%s">%s</a>' % (
		label, 
		title, 
		base_build_ref, 
		build_name,
		logical_build_name)


def make_png(src_dir, doc_dir, res_dir, tests_info_xml_f_name, build_name, test_name):
	cmd_str = '%s/scripts/make_graph.py %s %s %s %s %s' % (
		src_dir, doc_dir,
		res_dir, 
		tests_info_xml_f_name, 
		test_name, 
		build_name)
	# Must start a new process for pychart - otherwise pngs overlap.
	so = commands.getstatusoutput(cmd_str)	
	if(so[0] != 0):
		sys.stderr.write(cmd_str + '\n')		
		sys.stderr.write(so[1] + '\n')		
		sys.exit(-1)	


def make_png_str(label, test_name, build):
	ret = '<h6 class="c1">'
	ret += '<a name="%s" id= "%s">' % (label, label)
	ret += '<img src="%s" ' % (test_name + '_' + build + '.png')
	ret += 'alt="no image" />' 
	ret += '</a></h6>'	
	return ret

def process_html(html_f_name, src_dir, build_dir, htmls_xml_f_name, tests_info_xml_f_name, build_name, compiler_name):
	doc_dir = src_dir + "/docs/html/ext/pb_ds"
	res_dir = build_dir 
	html_f = open(doc_dir + '/' + html_f_name)	
	soup = BeautifulSoup(html_f.read())			
	html_f.close()	
	platform_comp_re = re.compile('platform_comp_%s' % build_name)					
	for d in soup('div'):
		try:
			settings_m = settings_div_re.match(d['id']) 
			res_m = res_div_re.match(d['id']) 
		except:
			settings_m = None
			res_m = None
			
		if settings_m:
			build = settings_m.groups()[0]			
			if build == build_name:
				logical_build_name = logical_build_from_build(build)
				info = gray_border_div_str
				info += '<h3><a name = "%s"><u>%s</u></a></h3>' % (build, logical_build_name)
				info += make_graph.comp_platform_info(compiler_name)
				info += '</div>%s</div>' % space_div_str
				d.contents = info
		elif res_m:
			label = res_m.groups()[0]
			d = d.divTag
			
			build = d['id'].replace('%s_' % label, '')
			
			if build == build_name:
				logical_build_name = logical_build_from_build(build)
				d = d.divTag
				test_name = d['id'].replace('%s_' % label, '')
				d = d.divTag
				base_build_ref = d['id'].replace('%s_' % label, '')
				d = d.divTag
				title = d['id'].replace('%s_' % label, '')
				img_title = img_title_from_origs(label, title, base_build_ref, build, logical_build_name)
					
				make_png(src_dir, doc_dir, res_dir, tests_info_xml_f_name, build_name, test_name)
				png_str = make_png_str(label, test_name, build)
				content = gray_border_div_str
				content += png_str
				content += img_title
#				content += make_graph.legend(doc_dir, res_dir, tests_info_xml_f_name, test_name, build_name)
				content += '</div>%s</div>' % space_div_str
				d.contents = content
								
	return soup
			
 
  
if __name__ == "__main__":
	"""
	Doc dir
	This module takes 6 parameters from the command line:
	Source directory
	Build directory
	HTMLs XML file name
	Tests info XML file name
	Build name
	Compiler name
	"""
	
	usg = "make_graph.py <src_dir> <build_dir> <htmls_xml_f_name> <tests_info_xml_f_name> <build_name> <compiler_name>\n"
		
	if len(sys.argv) != 7:
		sys.stderr.write(usg)
		raise exception
	
	src_dir = sys.argv[1]
	build_dir = sys.argv[2]
	htmls_xml_f_name = sys.argv[3]
	tests_info_xml_f_name = sys.argv[4]
	build_name = sys.argv[5]
	compiler_name = sys.argv[6]
	doc_dir = src_dir + "/docs/html/ext/pb_ds"
	htmls_dat = minidom.parse(htmls_xml_f_name)
	for html in htmls_dat.getElementsByTagName('html'):
		html_f_name = html.attributes['name'].value
		
		new_soup = process_html(html_f_name, src_dir, build_dir, htmls_xml_f_name, tests_info_xml_f_name, build_name, compiler_name)
		
		html_f = open(doc_dir + '/' + html_f_name, 'w')
		html_f.write(str(new_soup))
		html_f.close()
		
		
