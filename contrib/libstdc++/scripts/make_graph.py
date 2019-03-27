#!/usr/bin/python

import string
import sys
import re
import os
import platform
import commands
from Numeric import *
from pychart import *
from xml.dom import minidom

class exception:
	pass


def comp_platform_info(compiler):
	ret = '<ul>\n'
	so = commands.getstatusoutput('cat /proc/cpuinfo | grep \'cpu MHz\'')
	if so[0] == 0:
		ret += '<li>CPU speed - %s</li>\n' % so[1]
	so = commands.getstatusoutput('cat /proc/meminfo | grep \'MemTotal\'')
	if so[0] == 0:
		ret += '<li>Memory - %s</li>\n' % so[1]
	ret += '<li>Platform - %s</li>\n' % platform.platform()
	so = commands.getstatusoutput(compiler + ' --version')
	if so[0] == 0:
		ret += '<li>Compiler - %s</li>\n' % so[1]
	ret += '</ul>\n'
	return ret


class res:
	"""
	A 'structure' representing the results of a test.
	"""
	def __init__(self, x_label, y_label, cntnr_list, cntnr_descs, res_sets):
		self.x_label = x_label
		self.y_label = y_label
		self.cntnr_list = cntnr_list
		self.cntnr_descs = cntnr_descs
		self.res_sets = res_sets


class res_getter:
	"""
	This class returns a res object for some test.
	"""
	class __sorter:
		def __accum(self, results):
			total = 0
			for result in results:
				total = total + result[1]
			return total

		def sort(self, cntnr_list, res_sets):
			cntnrs_and_totals = []
			for cntnr in cntnr_list:
				results = res_sets[cntnr]
				total = self.__accum(results)
				cntnrs_and_totals.append((cntnr, total))
			by_total = lambda x,y: x[1] > y[1] and -1 or 1
			cntnrs_and_totals.sort(by_total)
			ret = []
			for cntnr_and_total in cntnrs_and_totals:
				cntnr = cntnr_and_total[0]
				ret.append(cntnr)
			return ret

	def __init__(self, test_infos_f_name):
		self.__test_to_container_res_sets = {}
		self.__test_to_f_names = {}
		tests_dat = minidom.parse(test_infos_f_name)
		for test in tests_dat.getElementsByTagName('test'):
			test_name = test.attributes['name'].value
			self.__test_to_f_names[test_name] = test.getElementsByTagName('file')[0].attributes['name'].value
			cntnr_list = []
			for cntnr in test.getElementsByTagName('cntnr'):
				cntnr_list.append(cntnr.attributes['name'].value)
			self.__test_to_container_res_sets[test_name] = cntnr_list

	def __get_label(self, tst_dat, label_name):
		label = tst_dat.getElementsByTagName(label_name)[0].firstChild.data
		label = string.strip(label, '\n')
		label = string.strip(label)
		return label

	def __parse_res_sets(self, f_name, cntnr_list):	
		tst_dat = minidom.parse(f_name)
		x_label = self.__get_label(tst_dat, 'x_name')
		y_label = self.__get_label(tst_dat, 'y_name')
		parsed_container_list = tst_dat.getElementsByTagName('cntnr')
		res_sets = {}
		cntnr_descs = {}
		for cntnr in parsed_container_list:
			cntnr_name = cntnr.attributes["name"].value
			res_sets[cntnr_name] = []
		for cntnr in parsed_container_list:
			cntnr_name = cntnr.attributes["name"].value
			cntnr_desc = cntnr.getElementsByTagName('desc')
			if res_sets.has_key(cntnr_name):
				res_set = []
				result_list = cntnr.getElementsByTagName('result')
				for result in result_list:
					x = string.atol(result.attributes["x"].value)
					y = string.atof(result.attributes["y"].value)
					res_set.append((x, y))
				res_sets[cntnr_name] = res_set
				cntnr_descs[cntnr_name] = cntnr_desc[0]
		return (x_label, y_label, cntnr_descs, res_sets)

	def get(self, res_dir, test_name):
		cntnr_list = self.__test_to_container_res_sets[test_name]
		f_name = res_dir + '/' + self.__test_to_f_names[test_name]
		parsed = self.__parse_res_sets(f_name, cntnr_list)
		x_label = parsed[0]
		y_label = parsed[1]
		cntnr_descs = parsed[2]
		res_sets = parsed[3]
		cntnr_list = self.__sorter().sort(cntnr_list, res_sets)
		return res(x_label, y_label, cntnr_list, cntnr_descs, res_sets)


class png_maker:
	"""
	This class creates a png file from a result set.
	"""
	class __style_chooser:
		def __init__(self):
			self.native_re = re.compile(r'n_(?:.*?)')

			self.native_tick_mark_0 = tick_mark.Circle(size = 4)
			self.native_tick_mark_1 = tick_mark.Square(size = 4)
			self.native_line_style_0 = line_style.T(color = color.black, width=2)
			self.native_line_style_1 = line_style.T(color = color.black, width=2)

			self.mask_re = re.compile(r'mask(?:.*?)')
			self.mod_re = re.compile(r'mod(?:.*?)')

			self.rb_tree_mmap_rb_tree_set_re = re.compile(r'rb_tree_mmap_rb_tree_set(?:.*?)')
			self.rb_tree_mmap_lu_mtf_set_re = re.compile(r'rb_tree_mmap_lu_mtf_set(?:.*?)')

			self.splay_re = re.compile(r'splay(?:.*?)')
			self.rb_tree_re = re.compile(r'rb_tree(?:.*?)')
			self.ov_tree_re = re.compile(r'ov_tree(?:.*?)')
			self.splay_tree_re = re.compile(r'splay_tree(?:.*?)')

			self.pat_trie_re = re.compile(r'pat_trie(?:.*?)')

			self.lc_1div8_1div2_re = re.compile(r'lc_1div8_1div2(?:.*?)')
			self.lc_1div8_1div1_re = re.compile(r'lc_1div8_1div1(?:.*?)')
			self.mcolc_1div2_re = re.compile(r'mcolc_1div2(?:.*?)')

		def choose(self, cntnr):
			if self.native_re.search(cntnr):
				if cntnr == 'n_pq_vector':
					return (self.native_tick_mark_1, self.native_line_style_1)

				return (self.native_tick_mark_0, self.native_line_style_0)

			# tick_mark predefined
			# square, circle3, dia, tri, dtri, star, plus5, x5, gray70dia, blackdtri, blackdia
			if self.mask_re.search(cntnr):
				clr = color.navy
			elif self.mod_re.search(cntnr):
				clr = color.green4
			elif self.rb_tree_mmap_rb_tree_set_re.search(cntnr):
				clr = color.mediumblue
				tm = tick_mark.square
			elif self.rb_tree_mmap_lu_mtf_set_re.search(cntnr) or cntnr == 'rc_binomial_heap':
				clr = color.gray50
				tm = tick_mark.dia
			elif self.splay_tree_re.search(cntnr) or cntnr == 'binomial_heap':
				clr = color.gray58
				tm = tick_mark.tri
			elif self.rb_tree_re.search(cntnr) or cntnr == 'binary_heap':
				clr = color.red3
				tm = tick_mark.dtri
			elif self.ov_tree_re.search(cntnr) or cntnr == 'thin_heap':
				clr = color.orangered1
				tm = tick_mark.star
			elif self.pat_trie_re.search(cntnr) or cntnr == 'pairing_heap':
				clr = color.blueviolet
				tm = tick_mark.plus5
			else:
				sys.stderr.write(cntnr + '\n')
				raise exception

                        # mask / mod
                        if cntnr.find('lc_1div8_1div') <> -1:
				if cntnr.find('mask') <> -1:
					# mask
					if self.lc_1div8_1div2_re.search(cntnr):
						if cntnr.find('nsth') <> -1:
							tm = tick_mark.x5
						else:
							tm = tick_mark.gray70dia
					if self.lc_1div8_1div1_re.search(cntnr):
						if cntnr.find('nsth') <> -1:
							tm = tick_mark.dia
                                                else:
							tm = tick_mark.circle3
                                else:
                                        # mod
					if self.lc_1div8_1div2_re.search(cntnr):
						if cntnr.find('nsth') <> -1:
							tm = tick_mark.tri
						else:
                                                        tm = tick_mark.square
					if self.lc_1div8_1div1_re.search(cntnr):
						if cntnr.find('nsth') <> -1:
                                                        tm = tick_mark.dtri
                                                else:
                                                        tm = tick_mark.star

			if self.mcolc_1div2_re.search(cntnr):
				tm = tick_mark.circle3
				
			return (tm, line_style.T(color = clr, width = 2))


	def __init__(self):
		self.__sc = self.__style_chooser()
		self.__mmap_re = re.compile('mmap_')

	def __container_label_name(self, cntnr):
		return self.__mmap_re.sub('\nmmap_\n', cntnr)

	def make(self, res, of_name):
		theme.output_format = 'png'
		theme.output_file = of_name
		theme.scale_factor = 2
#		theme.default_font_size = 5
		theme.use_color = 1
		theme.reinitialize()
		y_tick_interval = self.__get_y_tics(res)
		xaxis = axis.X(format = '/a90/hL%d',
			       tic_interval = 200,
			       label = res.x_label)
		yaxis = axis.Y(format = '%.2e', 
			       tic_interval = y_tick_interval,
			       label = res.y_label)
		legend_lines = len(res.cntnr_list)
		legend_vloc = 50 + (legend_lines * 10)
		ar = area.T(x_axis = xaxis, y_axis = yaxis,
			    legend = legend.T(loc=(0,-legend_vloc),
					      frame_line_style=None,
					      inter_row_sep=2),
			    size=(240,110))
		plot_list = []
		for cntnr in res.cntnr_list:
			style = self.__sc.choose(cntnr)
			print cntnr
			pl = line_plot.T(label = self.__container_label_name(cntnr), 
				data = res.res_sets[cntnr], 
				tick_mark = style[0], 
				line_style = style[1])
			plot_list.append(pl)
		for plot in plot_list:
			ar.add_plot(plot)
		ar.draw()


	def __get_y_tics(self, res):
		mx = 0
		for cntnr in res.cntnr_list:
			m = max(d[1] for d in res.res_sets[cntnr])
			mx = max(m, mx)
		return mx / 5 



def make_tt(s):
	return '<tt>' + s + '</tt>'

def make_b(s):
	return '<b>' + s + '</b>'

def make_ttb(s):
	return '<tt><b>' + s + '</b></tt>'

def make_i(s):
	return '<i>' + s + '</i>'

def make_pb_ds_class_href(c_name):
	return '<a href = "' + c_name + '.html">' + make_tt(c_name) + '</a>\n'

def build_value_to_pb_ds_class_href(s_desc):
	value = s_desc.attributes['value'].value
	ret = make_pb_ds_class_href(value)
	return ret

class hash_desc_to_html_builder:
	def build_specific_comb_hash_fn(self, s_desc):
		comb_hash_fn_desc = s_desc.getElementsByTagName('Comb_Hash_Fn')[0]
		ret = make_tt('Comb_Hash_Fn')
		ret = ret + ' = '
		ret = ret + build_value_to_pb_ds_class_href(comb_hash_fn_desc)
		return ret

	def __build_nom_denom(self, s_desc):
		nom_denom = s_desc.attributes['nom'].value + '/' + s_desc.attributes['denom'].value
		return make_i(nom_denom)

	def __build_lc_trigger_desc(self, s_desc):
		ret = build_value_to_pb_ds_class_href(s_desc)
		ret = ret + ' with ' + make_i('&alpha;<sub>min</sub>')
		ret = ret + ' = '  + self.__build_nom_denom(s_desc.getElementsByTagName('alpha_min')[0])
		ret = ret + ' and ' + make_i('&alpha;<sub>max</sub>')
		ret = ret + ' = '  + self.__build_nom_denom(s_desc.getElementsByTagName('alpha_max')[0])
		return ret

	def build_specific_resize_policy(self, s_desc):
		ret = make_tt('Resize_Policy')
		ret = ret + ' = '
		resize_policy_desc = s_desc.getElementsByTagName('Resize_Policy')[0]
		ret = ret + build_value_to_pb_ds_class_href(resize_policy_desc)
		ret = ret + ' with ' + make_tt('Size_Policy')
		ret = ret + ' = '
		size_policy_desc = resize_policy_desc.getElementsByTagName('Size_Policy')[0]
		ret = ret + build_value_to_pb_ds_class_href(size_policy_desc)
		ret = ret + ', and ' + make_tt('Trigger_Policy')
		ret = ret + ' = '
		trigger_policy_desc = resize_policy_desc.getElementsByTagName('Trigger_Policy')[0]
		if trigger_policy_desc.attributes['value'].value == 'hash_load_check_resize_trigger':
			ret = ret + self.__build_lc_trigger_desc(trigger_policy_desc)
		else:
			raise exception
		return ret


class cc_hash_desc_to_html_builder:
	def __init__(self):
		self.__hash_builder = hash_desc_to_html_builder()			

	def build(self, s_desc):
		ret = build_value_to_pb_ds_class_href(s_desc)
		ret = ret + 'with ' + self.__hash_builder.build_specific_comb_hash_fn(s_desc)
		ret = ret + ', and ' + self.__hash_builder.build_specific_resize_policy(s_desc)
		return ret


class gp_hash_desc_to_html_builder:
	def __init__(self):
		self.__hash_builder = hash_desc_to_html_builder()			

	def build(self, s_desc):
		ret = build_value_to_pb_ds_class_href(s_desc)
		ret = ret + ' with ' + self.__hash_builder.build_specific_comb_hash_fn(s_desc)
		ret = ret + ', ' + self.__hash_builder.build_specific_resize_policy(s_desc)
		ret = ret + ', and ' + make_tt('Probe_Fn')
		ret = ret + ' = '		
		probe_fn = s_desc.getElementsByTagName('Probe_Fn')[0].attributes['value'].value
		ret = ret + make_pb_ds_class_href(probe_fn)
		return ret


class basic_tree_like_desc_to_html_builder:
	def build_tag(self, s_desc):
		ret = make_tt('Tag')
		ret = ret + ' = '
		tag_desc = s_desc.getElementsByTagName('Tag')[0]
		ret = ret + build_value_to_pb_ds_class_href(tag_desc)
		return ret

	def build_node_update(self, s_desc):
		ret = make_tt('Node_Update')
		ret = ret + ' = '
		node_update_desc = s_desc.getElementsByTagName('Node_Update')[0]
		ret = ret + build_value_to_pb_ds_class_href(node_update_desc)
		return ret


class basic_tree_desc_to_html_builder:
	def __init__(self):
		self.__tree_like_builder = basic_tree_like_desc_to_html_builder()

	def build(self, s_desc):
		ret = build_value_to_pb_ds_class_href(s_desc)
		ret = ret + ' with ' + self.__tree_like_builder.build_tag(s_desc)
		ret = ret + ', and ' + self.__tree_like_builder.build_node_update(s_desc)
		return ret


class basic_trie_desc_to_html_builder:
	def __init__(self):
		self.__tree_like_builder = basic_tree_like_desc_to_html_builder()

	def build(self, s_desc):
		ret = build_value_to_pb_ds_class_href(s_desc)
		ret = ret + ' with ' + self.__tree_like_builder.build_tag(s_desc)
		ret = ret + ', and ' + self.__tree_like_builder.build_node_update(s_desc)
		return ret

class lu_desc_to_html_builder:
	def build(self, s_desc):
		ret = build_value_to_pb_ds_class_href(s_desc)
		ret = ret + ' with ' + make_tt('Update_Policy') 
		ret = ret + ' = ' 
		update_policy_desc = s_desc.getElementsByTagName('Update_Policy')[0]
		ret = ret + build_value_to_pb_ds_class_href(update_policy_desc)
		return ret


class std_desc_to_html_builder:
	def build(self, s_desc):
		value = s_desc.attributes['value'].value
		return make_tt(value.replace('std_', 'std::'))


class std_tr1_desc_to_html_builder:
	def build(self, s_desc):
		value = s_desc.attributes['value'].value
		ret =  make_tt(value.replace('std_tr1_', 'std::tr1::'))
		ret = ret + ' with ' + make_tt('cache_hash_code')
		ret = ret + ' = '
		cache_hash_code = s_desc.getElementsByTagName('cache_hash_code')[0].attributes['value'].value
		ret = ret + make_ttb(cache_hash_code)
		return ret

class gnucxx_desc_to_html_builder:
	def build(self, s_desc):
		value = s_desc.attributes['value'].value
		return make_tt(value.replace('__gnucxx_', '__gnucxx::'))

class stdext_desc_to_html_builder:
	def build(self, s_desc):
		value = s_desc.attributes['value'].value
		return make_tt(value.replace('stdext_', 'stdext::'))

class npq_desc_to_html_builder:
	def build(self, vector):
		if vector:
			under = make_tt('std::vector')
		else:
			under = make_tt('std::deque')

		return make_tt('std::priority_queue') + ' adapting ' + under

class binary_heap_desc_to_html_builder:
	def build(self, s_desc):
		ret = make_pb_ds_class_href('priority_queue')
		ret = ret + ' with ' + make_tt('Tag')
		ret = ret + ' = ' + make_pb_ds_class_href('binary_heap_tag')
		return ret

class thin_heap_desc_to_html_builder:
	def build(self, s_desc):
		ret = make_pb_ds_class_href('priority_queue')
		ret = ret + ' with ' + make_tt('Tag')
		ret = ret + ' = ' + make_pb_ds_class_href('thin_heap_tag')
		return ret

class binomial_heap_desc_to_html_builder:
	def build(self, s_desc):
		ret = make_pb_ds_class_href('priority_queue')
		ret = ret + ' with ' + make_tt('Tag')
		ret = ret + ' = ' + make_pb_ds_class_href('binomial_heap_tag')
		return ret

class rc_binomial_heap_desc_to_html_builder:
	def build(self, s_desc):
		ret = make_pb_ds_class_href('priority_queue')
		ret = ret + ' with ' + make_tt('Tag')
		ret = ret + ' = ' + make_pb_ds_class_href('rc_binomial_heap_tag')
		return ret

class pairing_heap_desc_to_html_builder:
	def build(self, s_desc):
		ret = make_pb_ds_class_href('priority_queue')
		ret = ret + ' with ' + make_tt('Tag')
		ret = ret + ' = ' + make_pb_ds_class_href('pairing_heap_tag')
		return ret

class legend_desc_builder:
	"""
	Returns a string corresponding to a specific container type.
	"""
	def __init__(self):
		self.__cc_hash_builder = cc_hash_desc_to_html_builder()
		self.__gp_hash_builder = gp_hash_desc_to_html_builder()	       
		self.__basic_tree_builder = basic_tree_desc_to_html_builder()
		self.__basic_trie_builder = basic_trie_desc_to_html_builder()
		self.__lu_builder = lu_desc_to_html_builder()
		self.__std_builder = std_desc_to_html_builder()
		self.__std_tr1_builder = std_tr1_desc_to_html_builder()
		self.__gnucxx_builder = gnucxx_desc_to_html_builder()
		self.__stdext_builder = stdext_desc_to_html_builder()
		self.__npq_builder = npq_desc_to_html_builder()
		self.__thin_heap_builder = thin_heap_desc_to_html_builder()
		self.__thin_heap_builder = thin_heap_desc_to_html_builder()
		self.__binary_heap_builder = binary_heap_desc_to_html_builder()
		self.__binomial_heap_builder = binomial_heap_desc_to_html_builder()
		self.__rc_binomial_heap_builder = rc_binomial_heap_desc_to_html_builder()
		self.__pairing_heap_builder = pairing_heap_desc_to_html_builder()

	def __build_specific(self, s_desc):
		type = s_desc.attributes['value'].value

		if type == 'thin_heap':
			return self.__thin_heap_builder.build(s_desc)
		if type == 'binary_heap':
			return self.__binary_heap_builder.build(s_desc)
		if type == 'binomial_heap':
			return self.__binomial_heap_builder.build(s_desc)
		if type == 'rc_binomial_heap':
			return self.__rc_binomial_heap_builder.build(s_desc)
		if type == 'pairing_heap':
			return self.__pairing_heap_builder.build(s_desc)
		if type == 'cc_hash_table':
			ret = self.__cc_hash_builder.build(s_desc)
		elif type == 'gp_hash_table':
			ret = self.__gp_hash_builder.build(s_desc)
		elif type == 'tree':
			ret = self.__basic_tree_builder.build(s_desc)
		elif type == 'trie':
			ret = self.__basic_trie_builder.build(s_desc)
		elif type == 'list_update':
			ret = self.__lu_builder.build(s_desc)
		elif type == 'std::priority_queue_vector':
			return self.__npq_builder.build(True)
		elif type == 'std::priority_queue_deque':
			return self.__npq_builder.build(False)
		elif type == 'std_set' or type == 'std_map' or type == 'std_multimap':
			return self.__std_builder.build(s_desc)
		elif type == 'std_tr1_unordered_set' or type == 'std_tr1_unordered_map':
			return self.__std_tr1_builder.build(s_desc)
		elif type == 'stdext_hash_set' or type == 'stdext_hash_map' or type == 'stdext_hash_multimap':
			return self.__stdext_builder.build(s_desc)
		elif type == '__gnucxx_hash_set' or type == '__gnucxx_hash_map' or type == '__gnucxx_hash_multimap':
			return self.__gnucxx_builder.build(s_desc)
		else:
			sys.stderr.write('cannot recognize %s\n' % type)
			raise exception
		return ret


	def build(self, desc):
		s_descs = desc.getElementsByTagName('type')
		if s_descs.length == 0:
			print desc.toxml()
			raise exception
		ret = ''
		count = 0
		for s_desc in s_descs:
			if count > 0:
				ret = ret + ', mapping each key to '
			ret = ret + self.__build_specific(s_desc)
			count = count + 1
		return ret


def main(doc_dir, res_dir, test_infos_f_name, test_name, build_name):
	res_gtr = res_getter(test_infos_f_name)
	res = res_gtr.get(res_dir, test_name)
	png_mkr = png_maker()
	png_of_name = doc_dir + '/' + test_name + '_' + build_name + '.png'
	print png_of_name
	png_mkr.make(res, png_of_name)


if __name__ == "__main__":
	"""
	This module takes 6 parameters from the command line:
	Docs directory
	Results directory
	Tests info XML file name
	Test name
	Build name
	Compiler name
	"""
	usg = "make_graph.py <doc_dir> <res_dir> <test_info_file> <test_name> <build_name>\n"
	if len(sys.argv) != 6:
		sys.stderr.write(usg)
		raise exception
	main(sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5])
